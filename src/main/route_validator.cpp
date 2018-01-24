#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <boost/format.hpp>
#include <boost/algorithm/string/join.hpp>

#include "route_validator.h"
#include "solver_wrapper.h"

namespace rows {

    std::ostream &operator<<(std::ostream &out, const rows::RouteValidator::ValidationError &error) {
        error.Print(out);
        return out;
    }

    std::ostream &operator<<(std::ostream &out, RouteValidator::ErrorCode error_code) {
        switch (error_code) {
            case RouteValidator::ErrorCode::MISSING_INFO:
                out << "MISSING_INFO";
                break;
            case RouteValidator::ErrorCode::TOO_MANY_CARERS:
                out << "TOO_MANY_CARERS";
                break;
            case RouteValidator::ErrorCode::LATE_ARRIVAL:
                out << "LATE_ARRIVAL";
                break;
            case RouteValidator::ErrorCode::BREAK_VIOLATION:
                out << "BREAK_VIOLATION";
                break;
            case RouteValidator::ErrorCode::ABSENT_CARER:
                out << "ABSENT_CARER";
                break;
            case RouteValidator::ErrorCode::UNKNOWN:
                out << "UNKNOWN";
                break;
            case RouteValidator::ErrorCode::MOVED:
                out << "MOVED";
                break;
        }
        return out;
    }

    RouteValidator::ValidationError::ValidationError(RouteValidator::ErrorCode error_code)
            : error_code_(error_code) {}

    RouteValidator::ErrorCode RouteValidator::ValidationError::error_code() const {
        return error_code_;
    }

    RouteValidator::RouteConflictError::RouteConflictError(const CalendarVisit &visit,
                                                           std::vector<rows::Route> routes)
            : ValidationError(ErrorCode::TOO_MANY_CARERS),
              visit_(visit),
              routes_(std::move(routes)) {}

    RouteValidator::ScheduledVisitError::ScheduledVisitError(ErrorCode error_code,
                                                             const ScheduledVisit &visit,
                                                             const rows::Route &route,
                                                             std::string error_message)
            : ValidationError(error_code),
              visit_(visit),
              route_(route),
              error_message_(std::move(error_message)) {}

    void RouteValidator::RouteConflictError::Print(std::ostream &out) const {
        std::vector<std::string> carers_text;
        for (const auto &route : routes_) {
            std::stringstream stream;
            stream << route.carer().sap_number();
            carers_text.emplace_back(stream.str());
        }

        out << (boost::format("RouteConflictError: visit %1% is scheduled to multiple carers: [%2%]")
                % visit_.service_user().id()
                % boost::algorithm::join(carers_text, ", ")).str();
    }

    const rows::CalendarVisit &RouteValidator::RouteConflictError::visit() const {
        return visit_;
    }

    const std::vector<rows::Route> &RouteValidator::RouteConflictError::routes() const {
        return routes_;
    }

    void RouteValidator::ScheduledVisitError::Print(std::ostream &out) const {
        out << (boost::format("ScheduledVisitError: %1%") % error_message_).str();
    }

    const rows::ScheduledVisit &RouteValidator::ScheduledVisitError::visit() const {
        return visit_;
    }

    std::vector<std::unique_ptr<rows::RouteValidator::ValidationError>>
    RouteValidator::Validate(const std::vector<rows::Route> &routes,
                             const rows::Problem &problem,
                             SolverWrapper &solver) const {
        std::vector<std::unique_ptr<rows::RouteValidator::ValidationError> > validation_errors;

        // find visits with incomplete information
        for (const auto &route: routes) {
            for (const auto &visit : route.visits()) {
                if (visit.type() == ScheduledVisit::VisitType::CANCELLED) {
                    continue;
                }

                if (!visit.calendar_visit().is_initialized()) {
                    validation_errors.emplace_back(CreateMissingInformationError(
                            route,
                            visit,
                            "calendar visit is missing"));
                } else if (!visit.location().is_initialized()) {
                    validation_errors.emplace_back(CreateMissingInformationError(
                            route,
                            visit,
                            "location is missing"));
                }
            }
        }

        // find visit assignment conflicts
        std::unordered_map<rows::CalendarVisit, std::vector<rows::Route> > visit_index;
        for (const auto &route: routes) {
            for (const auto &visit : route.visits()) {
                if (!IsAssignedAndActive(visit)) {
                    continue;
                }

                const auto &calendar_visit = visit.calendar_visit();
                const auto visit_index_it = visit_index.find(calendar_visit.get());
                if (visit_index_it != std::end(visit_index)) {
                    visit_index_it->second.push_back(route);
                } else {
                    visit_index.insert({calendar_visit.get(), std::vector<rows::Route> {route}});
                }
            }
        }

        for (const auto &visit_index_pair : visit_index) {
            if (visit_index_pair.second.size() > 1) {
                validation_errors.emplace_back(
                        std::make_unique<RouteConflictError>(visit_index_pair.first, visit_index_pair.second));
            }
        }

        for (const auto &route : routes) {
            std::vector<ScheduledVisit> visits_to_use;
            for (const auto &visit : route.visits()) {
                if (!IsAssignedAndActive(visit)) {
                    continue;
                }

                const auto index = solver.TryIndex(visit);
                if (!index.is_initialized()) {
                    validation_errors.emplace_back(CreateOrphanedError(route, visit));
                    continue;
                }

                if (visit.datetime() != visit.calendar_visit().get().datetime()
                    || visit.duration() != visit.calendar_visit().get().duration()) {
                    validation_errors.emplace_back(CreateMovedError(route, visit));
                    continue;
                }

                visits_to_use.push_back(visit);
            }

            if (visits_to_use.empty()) {
                continue;
            }

            const auto &carer = route.carer();
            const auto &diary = problem.diary(carer, visits_to_use[0].datetime().date());
            if (!diary.is_initialized()) {
                for (const auto &visit : visits_to_use) {
                    validation_errors.emplace_back(CreateAbsentCarerError(route, visit));
                }

                continue;
            }

            auto event_it = std::begin(diary.get().events());
            const auto event_it_end = std::end(diary.get().events());
            if (event_it == event_it_end) {
                for (const auto &visit : visits_to_use) {
                    validation_errors.emplace_back(CreateAbsentCarerError(route, visit));
                }

                continue;
            }

            // TODO: validation logic needs rewriting
            const auto visit_it_end = std::end(visits_to_use);
            std::vector<rows::ScheduledVisit> partial_route;
            for (auto visit_it = std::begin(visits_to_use); visit_it != visit_it_end; ++visit_it) {
                std::vector<rows::ScheduledVisit> route_candidate{partial_route};
                route_candidate.push_back(*visit_it);

                auto error_ptr = Validate(route_candidate, route, problem, solver);
                if (error_ptr) {
                    validation_errors.emplace_back(std::move(error_ptr));
                } else {
                    partial_route = std::move(route_candidate);
                }
            }

            /*
            boost::posix_time::ptime::time_duration_type last_service_duration = boost::posix_time::seconds(0);
            auto last_position = solver.depot();
            auto last_time = event_it->begin().time_of_day();
            auto break_start = event_it->end().time_of_day();

            while (visit_it != visit_it_end) {
                auto current_location = visit_it->location().get();
                auto current_time = last_time
                                    + last_service_duration
                                    + solver.TravelTime(last_position, visit_it->location().get());

                if (current_time >= break_start) {
                    break;
                }

                auto earliest_arrival_time = static_cast<decltype(last_time)>(boost::posix_time::seconds(
                        solver.GetBeginWindow(visit_it->datetime().time_of_day())));
                auto latest_arrival_time = static_cast<decltype(last_time)>(boost::posix_time::seconds(
                        solver.GetEndWindow(visit_it->datetime().time_of_day())));
                const auto current_arrival = current_time = std::max(earliest_arrival_time, current_time);
                if (current_time >= latest_arrival_time) {
                    validation_errors.emplace_back(
                            CreateLateArrivalError(route, *visit_it, current_time - latest_arrival_time));
                    ++visit_it;
                    break;
                } else {
                    const auto current_service_duration = visit_it->duration();
                    current_time += current_service_duration;

                    auto next_location = solver.depot();
                    auto prev_visit_it = visit_it++;
                    if (visit_it != visit_it_end) {
                        next_location = visit_it->location().get();
                    }

                    current_time += solver.TravelTime(current_location, next_location);
                    if (current_time >= break_start) {
                        validation_errors.emplace_back(CreateContractualBreakViolationError(route, *prev_visit_it));
                        break;
                    } else {
                        last_position = current_location;
                        last_service_duration = current_service_duration;
                        last_time = current_arrival;
                    }
                }
            }

            while (visit_it != visit_it_end) {
                validation_errors.emplace_back(CreateContractualBreakViolationError(route, *visit_it));
                ++visit_it;
            }*/
        }

        return validation_errors;
    }

    bool rows::RouteValidator::IsAssignedAndActive(const rows::ScheduledVisit &visit) {
        return visit.calendar_visit().is_initialized()
               && visit.carer().is_initialized()
               && visit.type() == ScheduledVisit::VisitType::UNKNOWN;
    }

    std::unique_ptr<rows::RouteValidator::ValidationError> rows::RouteValidator::TryPerformVisit(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            const rows::Problem &problem,
            rows::SolverWrapper &solver,
            rows::Location &location,
            boost::posix_time::time_duration &time) const {
        boost::posix_time::time_duration time_to_use{time};

        const auto &diary = problem.diary(route.carer(), visit.datetime().date()).get();
        auto work_interval_it = std::begin(diary.events());
        const auto work_interval_end_it = std::end(diary.events());

        // find current work interval
        for (; work_interval_it != work_interval_end_it
               && work_interval_it->end().time_of_day() <= time_to_use;
               ++work_interval_it);

        if (work_interval_it == work_interval_end_it) {
            return CreateContractualBreakViolationError(route, visit);
        }

        if (visit.location() != location) {
            time_to_use += solver.TravelTime(location, visit.location().get());
        }

        const auto visit_window_begin = static_cast<boost::posix_time::time_duration>(boost::posix_time::seconds(
                solver.GetBeginWindow(visit.datetime().time_of_day())));
        const auto visit_window_end = static_cast<boost::posix_time::time_duration>(boost::posix_time::seconds(
                solver.GetEndWindow(visit.datetime().time_of_day())));
        time_to_use = std::max(time_to_use, visit_window_begin);
        if (time_to_use > visit_window_end) {
            return CreateLateArrivalError(route, visit, time_to_use - visit_window_end);
        }

        time_to_use += visit.duration();
        if (time_to_use > work_interval_it->end().time_of_day()) {
            return CreateContractualBreakViolationError(route, visit);
        }

        // visit can be performed
        location = visit.location().get();
        time = time_to_use;
        return nullptr;

        /*
        // find effective time of approaching the destination
        if (visit.location() != location) {
            auto remaining_travel_duration = solver.TravelTime(location, visit.location().get());
            while (remaining_travel_duration.total_seconds() > 0) {
                if (time_to_use + remaining_travel_duration <= work_interval_it->end().time_of_day()) {
                    time_to_use += remaining_travel_duration;
                    remaining_travel_duration = boost::posix_time::seconds(0);

                    if (time_to_use == work_interval_it->end().time_of_day()) {
                        ++work_interval_it;

                        if (work_interval_it == work_interval_end_it) {
                            return CreateContractualBreakViolationError(route, visit);
                        }

                        time_to_use = work_interval_it->begin().time_of_day();
                    }

                    break;
                }

                remaining_travel_duration -= work_interval_it->end().time_of_day() - time_to_use;

                ++work_interval_it;
                if (work_interval_it == work_interval_end_it) {
                    return CreateContractualBreakViolationError(route, visit);
                }

                time_to_use = work_interval_it->begin().time_of_day();
            }
        }

        const auto visit_window_begin = static_cast<boost::posix_time::time_duration>(boost::posix_time::seconds(
                solver.GetBeginWindow(visit.datetime().time_of_day())));
        const auto visit_window_end = static_cast<boost::posix_time::time_duration>(boost::posix_time::seconds(
                solver.GetEndWindow(visit.datetime().time_of_day())));
        time_to_use = std::max(time_to_use, visit_window_begin);
        if (time_to_use > visit_window_end) {
            return CreateLateArrivalError(route, visit, time_to_use - visit_window_end);
        }

        while (true) {
            if (time_to_use > visit_window_end) {
                return CreateLateArrivalError(route, visit, time_to_use - visit_window_end);
            }

            auto service_finish = time_to_use + visit.duration();
            if (service_finish <= work_interval_it->end().time_of_day()) {
                // visit can be performed
                location = visit.location().get();
                time = service_finish;
                return nullptr;
            }

            ++work_interval_it;
            if (work_interval_it == work_interval_end_it) {
                return CreateContractualBreakViolationError(route, visit);
            }

            time_to_use = work_interval_it->begin().time_of_day();
        }*/
    }

    std::unique_ptr<RouteValidator::ValidationError> RouteValidator::CreateAbsentCarerError(
            const rows::Route &route,
            const ScheduledVisit &visit) const {
        return std::make_unique<ScheduledVisitError>(
                ErrorCode::ABSENT_CARER,
                visit,
                route,
                (boost::format("Carer %1% is absent on the visit %2% day.")
                 % route.carer().sap_number()
                 % visit.service_user().get().id()).str());
    }

    std::unique_ptr<RouteValidator::ValidationError> RouteValidator::CreateLateArrivalError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            const boost::posix_time::ptime::time_duration_type delay) const {
        return std::make_unique<ScheduledVisitError>(
                ErrorCode::LATE_ARRIVAL,
                visit,
                route,
                (boost::format("Carer %1% arrives with a delay of %2% to the visit %3%.")
                 % visit.carer().get().sap_number()
                 % delay
                 % visit.service_user().get().id()).str());
    }

    std::unique_ptr<RouteValidator::ValidationError> RouteValidator::CreateContractualBreakViolationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit) const {
        return std::make_unique<ScheduledVisitError>(
                ErrorCode::BREAK_VIOLATION,
                visit,
                route,
                (boost::format("The visit %1% violates contractual breaks of the carer %2%.")
                 % visit.service_user().get().id()
                 % route.carer().sap_number()).str());
    }

    std::unique_ptr<RouteValidator::ValidationError> RouteValidator::CreateContractualBreakViolationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            std::vector<rows::Event> overlapping_slots) const {
        if (overlapping_slots.empty()) {
            return CreateContractualBreakViolationError(route, visit);
        } else {
            std::vector<std::string> slot_texts;
            for (const auto &event : overlapping_slots) {
                std::stringstream message;
                message << "[" << event.begin().time_of_day()
                        << ", " << event.end().time_of_day()
                        << "]";
                slot_texts.emplace_back(message.str());
            }

            std::string joined_slot_text;
            if (slot_texts.size() == 1) {
                joined_slot_text = slot_texts[0];
            } else {
                joined_slot_text = boost::algorithm::join(slot_texts, ", ");
            }

            return std::make_unique<ScheduledVisitError>(
                    ErrorCode::BREAK_VIOLATION,
                    visit,
                    route,
                    (boost::format(
                            "The visit %1% violates contractual breaks of the carer %2%: [%3%, %4%] does not fit into %5%.")
                     % visit.service_user().get().id()
                     % route.carer().sap_number()
                     % visit.datetime().time_of_day()
                     % (visit.datetime().time_of_day() + visit.duration())
                     % joined_slot_text).str());
        }
    }

    std::unique_ptr<RouteValidator::ValidationError> RouteValidator::CreateMissingInformationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            std::string error_msg) const {
        return std::make_unique<ScheduledVisitError>(ErrorCode::MISSING_INFO, visit, route, error_msg);
    }

    std::unique_ptr<rows::RouteValidator::ValidationError>
    RouteValidator::CreateOrphanedError(const Route &route, const ScheduledVisit &visit) const {
        return std::make_unique<ScheduledVisitError>(ErrorCode::ORPHANED,
                                                     visit,
                                                     route,
                                                     (boost::format(
                                                             "The visit %1% is not present in the problem definition.")
                                                      % visit).str());
    }

    std::unique_ptr<rows::RouteValidator::ValidationError> RouteValidator::CreateMovedError(
            const Route &route,
            const ScheduledVisit &visit) const {
        std::string error_msg;

        const auto &calendar_visit = visit.calendar_visit().get();
        if (visit.datetime() != calendar_visit.datetime()) {
            error_msg = (boost::format(
                    "The visit %1% datetime was moved from %2% to %3%.")
                         % visit
                         % calendar_visit.datetime()
                         % visit.datetime()).str();
        } else if (visit.duration() != calendar_visit.duration()) {
            error_msg = (boost::format(
                    "The visit %1% duration was changed from %2% to %3%.")
                         % visit
                         % calendar_visit.datetime()
                         % visit.datetime()).str();
        }

        return std::make_unique<ScheduledVisitError>(ErrorCode::MOVED,
                                                     visit,
                                                     route,
                                                     error_msg);
    }

    std::unique_ptr<RouteValidator::ValidationError>
    RouteValidator::Validate(const std::vector<rows::ScheduledVisit> &partial_route,
                             const rows::Route &route,
                             const rows::Problem &problem,
                             rows::SolverWrapper &solver) const {
        if (partial_route.empty()) {
            return nullptr;
        }

        const auto diary = problem.diary(route.carer(), partial_route.front().datetime().date());

        auto work_interval_it = std::begin(diary.get().events());
        const auto work_interval_end_it = std::end(diary.get().events());

        if (work_interval_it == work_interval_end_it) {
            return CreateContractualBreakViolationError(route, partial_route.back());
        }

        std::stringstream stream;
        std::vector<std::string> text_locations;
        std::transform(std::cbegin(partial_route),
                       std::cend(partial_route),
                       std::back_inserter(text_locations),
                       [](const rows::ScheduledVisit &visit) -> std::string {
                           std::stringstream local_stream;
                           local_stream << visit.location().get();
                           return local_stream.str();
                       });
        stream << "Validating path: " << boost::algorithm::join(text_locations, ", ")
               << " within work interval ["
               << work_interval_it->begin()
               << ", "
               << work_interval_it->end()
               << "]";
        LOG(INFO) << stream.str();

        while (work_interval_it != work_interval_end_it
               && partial_route[0].datetime() < work_interval_it->begin()) {
            ++work_interval_it;
        }

        if (work_interval_it == work_interval_end_it) {
            return CreateContractualBreakViolationError(route, partial_route.front());
        }

        auto last_time = work_interval_it->begin().time_of_day();
        auto last_position = solver.depot();

        for (const auto &visit : partial_route) {
            const auto earliest_arrival = static_cast<boost::posix_time::time_duration>(
                    boost::posix_time::seconds(solver.GetBeginWindow(visit.datetime().time_of_day())));
            const auto latest_arrival = static_cast<boost::posix_time::time_duration>(
                    boost::posix_time::seconds(solver.GetEndWindow(visit.datetime().time_of_day())));
            const auto travel_time = solver.TravelTime(last_position, visit.location().get());
            const auto current_arrival = last_time + travel_time;

            if (current_arrival > work_interval_it->end().time_of_day()) {
                return CreateContractualBreakViolationError(route, partial_route.back());
            }

            const auto service_start = std::max(current_arrival, earliest_arrival);
//
//            while (work_interval_it != work_interval_end_it
//                   && service_start >= work_interval_it->end().time_of_day()) {
//                ++work_interval_it;
//            }
//
//            if (work_interval_it == work_interval_end_it) {
//                return CreateContractualBreakViolationError(route, partial_route.back());
//            }

            if (service_start >= latest_arrival) {
                std::stringstream local_msg_stream;
                local_msg_stream << "\t\t [violation]"
                                 << " approached: " << visit.location().get()
                                 << " [ " << earliest_arrival << "," << latest_arrival << " ]"
                                 << " travelled: " << travel_time
                                 << " arrived: " << current_arrival
                                 << " service_start: " << service_start
                                 << " latest_service_start: : " << latest_arrival;

                LOG(INFO) << local_msg_stream.str();
                return CreateLateArrivalError(route, visit, service_start - latest_arrival);
            }

            const auto service_finish = service_start + visit.duration();
            if (service_finish >= work_interval_it->end().time_of_day()) {
                std::stringstream local_msg_stream;
                local_msg_stream << "\t\t [violation]"
                                 << " approached: " << visit.location().get()
                                 << " [ " << earliest_arrival << "," << latest_arrival << " ]"
                                 << " travelled: " << travel_time
                                 << " arrived: " << current_arrival
                                 << " service_start: " << service_start
                                 << " completed_service: " << service_finish
                                 << " planned_break: " << work_interval_it->end().time_of_day();

                LOG(INFO) << local_msg_stream.str();
                return CreateContractualBreakViolationError(route, partial_route.back());
            }

            std::stringstream msg_stream;
            msg_stream << "\t\t --> approached: " << visit.location().get()
                       << " [ " << earliest_arrival << "," << latest_arrival << " ]"
                       << " travelled: " << travel_time
                       << " arrived: " << current_arrival
                       << " started_service: " << service_start
                       << " completed_service: " << service_finish;
            LOG(INFO) << msg_stream.str();
            last_time = service_finish;
            last_position = visit.location().get();
        }

        last_time += solver.TravelTime(last_position, solver.depot());
        if (last_time > work_interval_it->end().time_of_day()) {
            return CreateContractualBreakViolationError(route, partial_route.back());
        }

//        int64 raw_last_time = work_interval_it->begin().time_of_day().total_seconds();
//        auto last_index = SolverWrapper::DEPOT;
//        for (const auto &visit : partial_route) {
//            const auto current_index = solver.Index(visit);
//            const auto earliest_arrival = solver.GetBeginWindow(visit.datetime().time_of_day());
//            const auto latest_arrival = solver.GetEndWindow(visit.datetime().time_of_day());
//            const auto current_arrival = raw_last_time + solver.ServicePlusTravelTime(last_index, current_index);
//            const auto service_start = std::max(current_arrival, earliest_arrival);
//
//            if (service_start >= latest_arrival) {
//                std::stringstream local_msg_stream;
//                local_msg_stream << "\t\t [violation]"
//                                 << " approached: " << visit.location().get()
//                                 << " [ " << boost::posix_time::seconds(earliest_arrival) << ","
//                                 << boost::posix_time::seconds(latest_arrival) << " ]"
//                                 << " arrived: " << boost::posix_time::seconds(current_arrival)
//                                 << " service_start: " << boost::posix_time::seconds(service_start)
//                                 << " latest_service_start: : " << boost::posix_time::seconds(latest_arrival);
//
//                LOG(INFO) << local_msg_stream.str();
//                return CreateLateArrivalError(route, visit, boost::posix_time::seconds(service_start - latest_arrival));
//            }
//
//            std::stringstream msg_stream;
//            msg_stream << "\t\t --> approached: " << visit.location().get()
//                       << " [ " << boost::posix_time::seconds(earliest_arrival) << ","
//                       << boost::posix_time::seconds(latest_arrival) << " ]"
//                       << " arrived: " << boost::posix_time::seconds(current_arrival)
//                       << " service_start: " << boost::posix_time::seconds(service_start);
//            LOG(INFO) << msg_stream.str();
//            raw_last_time = service_start;
//            last_index = current_index;
//        }
//
//        raw_last_time += solver.ServicePlusTravelTime(last_index, SolverWrapper::DEPOT);
//        if (raw_last_time > work_interval_it->end().time_of_day().total_seconds()) {
//            return CreateContractualBreakViolationError(route, partial_route.back());
//        }

        return nullptr;
    }
}
