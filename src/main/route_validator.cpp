#include <algorithm>
#include <sstream>
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
        std::vector<std::unique_ptr<rows::RouteValidator::ValidationError>> validation_errors;

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

            auto current_time = event_it->begin().time_of_day();
            auto current_position = visits_to_use[0].location().get();
            for (const auto &visit : visits_to_use) {
                auto error_ptr = TryPerformVisit(route,
                                                 visit,
                                                 problem,
                                                 solver,
                                                 current_position,
                                                 current_time);
                if (error_ptr) {
                    validation_errors.emplace_back(std::move(error_ptr));
                }
            }
        }

        return validation_errors;
    }

    bool rows::RouteValidator::IsAssignedAndActive(const rows::ScheduledVisit &visit) {
        return visit.calendar_visit().is_initialized()
               && visit.carer().is_initialized()
               && visit.type() != ScheduledVisit::VisitType::CANCELLED;
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
}
