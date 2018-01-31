#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <boost/format.hpp>
#include <boost/date_time.hpp>
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
            case RouteValidator::ErrorCode::ORPHANED:
                out << "ORPHANED";
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
                    validation_errors.emplace_back(std::make_unique<ScheduledVisitError>(CreateMissingInformationError(
                            route,
                            visit,
                            "calendar visit is missing")));
                } else if (!visit.location().is_initialized()) {
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(CreateMissingInformationError(
                                    route,
                                    visit,
                                    "location is missing")));
                }
            }
        }

        // find visit assignment conflicts
        std::unordered_map<rows::CalendarVisit, std::vector<rows::Route>> visit_index;
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
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(CreateOrphanedError(route, visit)));
                    continue;
                }

                if (visit.datetime() != visit.calendar_visit().get().datetime()
                    || visit.duration() != visit.calendar_visit().get().duration()) {
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(CreateMovedError(route, visit)));
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
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(CreateAbsentCarerError(route, visit)));
                }

                continue;
            }

            auto event_it = std::begin(diary.get().events());
            const auto event_it_end = std::end(diary.get().events());
            if (event_it == event_it_end) {
                for (const auto &visit : visits_to_use) {
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(CreateAbsentCarerError(route, visit)));
                }

                continue;
            }

            const auto visit_it_end = std::end(visits_to_use);
            Route partial_route{carer};
            for (auto visit_it = std::begin(visits_to_use); visit_it != visit_it_end; ++visit_it) {
                Route route_candidate{partial_route};
                route_candidate.visits().push_back(*visit_it);

                auto validation_result = Validate(route_candidate, solver);
                if (static_cast<bool>(validation_result.error())) {
                    validation_errors.emplace_back(std::move(validation_result.error()));
                } else {
                    partial_route = std::move(route_candidate);
                }
            }
        }

        return validation_errors;
    }

    bool rows::RouteValidator::IsAssignedAndActive(const rows::ScheduledVisit &visit) {
        return visit.calendar_visit().is_initialized()
               && visit.carer().is_initialized()
               && visit.type() == ScheduledVisit::VisitType::UNKNOWN;
    }

    RouteValidator::ScheduledVisitError RouteValidator::CreateAbsentCarerError(
            const rows::Route &route,
            const ScheduledVisit &visit) const {
        return {ErrorCode::ABSENT_CARER,
                visit,
                route,
                (boost::format("Carer %1% is absent on the visit %2% day.")
                 % route.carer().sap_number()
                 % visit.service_user().get().id()).str()};
    }

    RouteValidator::ScheduledVisitError RouteValidator::CreateLateArrivalError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            const boost::posix_time::ptime::time_duration_type delay) const {
        return {ErrorCode::LATE_ARRIVAL,
                visit,
                route,
                (boost::format("Carer %1% arrives with a delay of %2% to the visit %3%.")
                 % visit.carer().get().sap_number()
                 % delay
                 % visit.service_user().get().id()).str()};
    }

    RouteValidator::ScheduledVisitError RouteValidator::CreateContractualBreakViolationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit) const {
        return {ErrorCode::BREAK_VIOLATION,
                visit,
                route,
                (boost::format("The visit %1% violates contractual breaks of the carer %2%.")
                 % visit.service_user().get().id()
                 % route.carer().sap_number()).str()};
    }

    RouteValidator::ScheduledVisitError RouteValidator::CreateContractualBreakViolationError(
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

            return {ErrorCode::BREAK_VIOLATION,
                    visit,
                    route,
                    (boost::format(
                            "The visit %1% violates contractual breaks of the carer %2%: [%3%, %4%] does not fit into %5%.")
                     % visit.service_user().get().id()
                     % route.carer().sap_number()
                     % visit.datetime().time_of_day()
                     % (visit.datetime().time_of_day() + visit.duration())
                     % joined_slot_text).str()};
        }
    }

    RouteValidator::ScheduledVisitError RouteValidator::CreateMissingInformationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            std::string error_msg) const {
        return {ErrorCode::MISSING_INFO, visit, route, std::move(error_msg)};
    }

    RouteValidator::ScheduledVisitError
    RouteValidator::CreateOrphanedError(const Route &route, const ScheduledVisit &visit) const {
        return {ErrorCode::ORPHANED,
                visit,
                route,
                (boost::format("The visit %1% is not present in the problem definition.")
                 % visit).str()};
    }

    RouteValidator::ScheduledVisitError RouteValidator::CreateMovedError(
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

        return {ErrorCode::MOVED, visit, route, error_msg};
    }

    RouteValidator::ValidationResult RouteValidator::Validate(const Route &route,
                                                              SolverWrapper &solver) const {
        boost::posix_time::time_duration total_available_time{boost::posix_time::seconds(0)};
        boost::posix_time::time_duration total_service_time{boost::posix_time::seconds(0)};
        boost::posix_time::time_duration total_travel_time{boost::posix_time::seconds(0)};

        const auto &visits = route.visits();
        if (visits.empty()) {
            return RouteValidator::ValidationResult{
                    RouteValidator::Metrics{total_available_time, total_service_time, total_travel_time}};
        }

        const auto diary = solver.Diary(route.carer(), visits.front().datetime().date());

        auto work_interval_it = std::begin(diary.get().events());
        const auto work_interval_end_it = std::end(diary.get().events());

        if (work_interval_it == work_interval_end_it) {
            return RouteValidator::ValidationResult{
                    std::make_unique<ScheduledVisitError>(CreateContractualBreakViolationError(route, visits.back()))};
        }

        for (auto interval_it = work_interval_it; interval_it != work_interval_end_it; ++work_interval_it) {
            total_available_time += interval_it->duration();
        }

        if (VLOG_IS_ON(1)) {
            std::vector<std::string> text_locations;
            std::transform(std::cbegin(visits),
                           std::cend(visits),
                           std::back_inserter(text_locations),
                           [](const rows::ScheduledVisit &visit) -> std::string {
                               std::stringstream local_stream;
                               local_stream << visit.location().get();
                               return local_stream.str();
                           });

            std::vector<std::string> text_intervals;
            auto local_work_interval_it = work_interval_it;
            std::transform(local_work_interval_it,
                           work_interval_end_it,
                           std::back_inserter(text_intervals),
                           [](const rows::Event &event) -> std::string {
                               std::stringstream local_stream;
                               local_stream << event;
                               return local_stream.str();
                           });

            VLOG(1) << "Validating path: " << boost::algorithm::join(text_locations, ", ")
                    << " within work intervals: " << boost::algorithm::join(text_intervals, ", ");
        }

        while (work_interval_it != work_interval_end_it
               && visits[0].datetime() < work_interval_it->begin()) {
            ++work_interval_it;
        }

        if (work_interval_it == work_interval_end_it) {
            return RouteValidator::ValidationResult{
                    std::make_unique<ScheduledVisitError>(CreateContractualBreakViolationError(route, visits.front()))};
        }

        auto last_time = work_interval_it->begin().time_of_day();
        auto last_position = solver.depot();

        for (const auto &visit : visits) {
            const auto earliest_arrival = static_cast<boost::posix_time::time_duration>(
                    boost::posix_time::seconds(solver.GetBeginWindow(visit.datetime().time_of_day())));
            const auto latest_arrival = static_cast<boost::posix_time::time_duration>(
                    boost::posix_time::seconds(solver.GetEndWindow(visit.datetime().time_of_day())));
            const auto travel_time = solver.TravelTime(last_position, visit.location().get());
            total_travel_time += travel_time;
            const auto current_arrival = last_time + travel_time;

            if (current_arrival > work_interval_it->end().time_of_day()) {
                return RouteValidator::ValidationResult{std::make_unique<ScheduledVisitError>(
                        CreateContractualBreakViolationError(route, visits.back()))};
            }

            const auto &service_start = std::max(current_arrival, earliest_arrival);

            while (work_interval_it != work_interval_end_it
                   && (service_start >= work_interval_it->end().time_of_day()
                       || service_start < work_interval_it->begin().time_of_day())) {
                ++work_interval_it;
            }

            if (work_interval_it == work_interval_end_it) {
                return RouteValidator::ValidationResult{std::make_unique<ScheduledVisitError>(
                        CreateContractualBreakViolationError(route, visits.back()))};
            }

            if (service_start > latest_arrival) {
                VLOG(1) << "[LATEST_ARRIVAL_CONSTRAINT_VIOLATION] "
                        << " approached: " << visit.location().get()
                        << " [ " << earliest_arrival << "," << latest_arrival << " ]"
                        << " travelled: " << travel_time
                        << " arrived: " << current_arrival
                        << " service_start: " << service_start
                        << " latest_service_start: : " << latest_arrival;
                return RouteValidator::ValidationResult{std::make_unique<ScheduledVisitError>(
                        CreateLateArrivalError(route, visit, service_start - latest_arrival))};
            }

            const auto service_finish = service_start + visit.duration();
            total_service_time += visit.duration();

            if (service_finish > work_interval_it->end().time_of_day()) {
                VLOG(1) << "[BREAK_CONSTRAINT_VIOLATION]"
                        << " approached: " << visit.location().get()
                        << " [ " << earliest_arrival << "," << latest_arrival << " ]"
                        << " travelled: " << travel_time
                        << " arrived: " << current_arrival
                        << " service_start: " << service_start
                        << " completed_service: " << service_finish
                        << " planned_break: " << work_interval_it->end().time_of_day();

                return RouteValidator::ValidationResult{std::make_unique<ScheduledVisitError>(
                        CreateContractualBreakViolationError(route, visits.back()))};
            }

            VLOG(1) << "approached: " << visit.location().get()
                    << " [ " << earliest_arrival << "," << latest_arrival << " ]"
                    << " travelled: " << travel_time
                    << " arrived: " << current_arrival
                    << " started_service: " << service_start
                    << " completed_service: " << service_finish;

            last_time = service_finish;
            last_position = visit.location().get();
        }

        last_time += solver.TravelTime(last_position, solver.depot());
        if (last_time > work_interval_it->end().time_of_day()) {
            return RouteValidator::ValidationResult{
                    std::make_unique<ScheduledVisitError>(CreateContractualBreakViolationError(route, visits.back()))};
        }

        return RouteValidator::ValidationResult{
                RouteValidator::Metrics{total_available_time, total_service_time, total_travel_time}};
    }

    RouteValidator::ValidationResult::ValidationResult()
            : metrics_{},
              error_{nullptr} {}

    RouteValidator::ValidationResult::ValidationResult(RouteValidator::Metrics metrics)
            : metrics_{std::move(metrics)},
              error_{nullptr} {}

    RouteValidator::ValidationResult::ValidationResult(
            std::unique_ptr<RouteValidator::ValidationError> &&error) noexcept
            : metrics_{},
              error_{std::move(error)} {}

    RouteValidator::ValidationResult::ValidationResult(RouteValidator::ValidationResult &&other) noexcept
            : metrics_{},
              error_{std::move(other.error_)} {}

    std::unique_ptr<RouteValidator::ValidationError> &RouteValidator::ValidationResult::error() {
        return error_;
    }

    const std::unique_ptr<RouteValidator::ValidationError> &RouteValidator::ValidationResult::error() const {
        return error_;
    }

    RouteValidator::ValidationResult &RouteValidator::ValidationResult::operator=(
            RouteValidator::ValidationResult &&other) noexcept {
        metrics_ = std::move(other.metrics_);
        error_ = std::move(other.error_);
        return *this;
    }

    const RouteValidator::Metrics &RouteValidator::ValidationResult::metrics() const {
        return metrics_;
    }

    RouteValidator::Metrics::Metrics()
            : Metrics({}, {}, {}) {}

    RouteValidator::Metrics::Metrics(boost::posix_time::time_duration available_time,
                                     boost::posix_time::time_duration service_time,
                                     boost::posix_time::time_duration travel_time)
            : available_time_(std::move(available_time)),
              service_time_(std::move(service_time)),
              travel_time_(std::move(travel_time)) {}

    RouteValidator::Metrics::Metrics(const RouteValidator::Metrics &metrics)
            : available_time_(metrics.available_time_),
              service_time_(metrics.service_time_),
              travel_time_(metrics.travel_time_) {}

    RouteValidator::Metrics::Metrics(RouteValidator::Metrics &&metrics) noexcept
            : available_time_(std::move(metrics.available_time_)),
              service_time_(std::move(metrics.service_time_)),
              travel_time_(std::move(metrics.travel_time_)) {}

    RouteValidator::Metrics &RouteValidator::Metrics::operator=(const RouteValidator::Metrics &metrics) {
        available_time_ = metrics.available_time_;
        service_time_ = metrics.service_time_;
        travel_time_ = metrics.travel_time_;
        return *this;
    }

    RouteValidator::Metrics &RouteValidator::Metrics::operator=(RouteValidator::Metrics &&metrics) noexcept {
        available_time_ = std::move(metrics.available_time_);
        service_time_ = std::move(metrics.service_time_);
        travel_time_ = std::move(metrics.travel_time_);
        return *this;
    }

    boost::posix_time::time_duration RouteValidator::Metrics::available_time() const {
        return available_time_;
    }

    boost::posix_time::time_duration RouteValidator::Metrics::service_time() const {
        return service_time_;
    }

    boost::posix_time::time_duration RouteValidator::Metrics::travel_time() const {
        return travel_time_;
    }

    boost::posix_time::time_duration RouteValidator::Metrics::idle_time() const {
        return available_time_ - service_time_ - travel_time_;
    }


}
