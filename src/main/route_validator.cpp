#include <algorithm>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <boost/format.hpp>
#include <boost/date_time.hpp>
#include <boost/algorithm/string/join.hpp>

#include "util/date_time.h"
#include "route_validator.h"
#include "solver_wrapper.h"

namespace rows {

    std::ostream &operator<<(std::ostream &out, const rows::RouteValidatorBase::ValidationError &error) {
        error.Print(out);
        return out;
    }

    std::ostream &operator<<(std::ostream &out, RouteValidatorBase::ErrorCode error_code) {
        switch (error_code) {
            case RouteValidatorBase::ErrorCode::MISSING_INFO:
                out << "MISSING_INFO";
                break;
            case RouteValidatorBase::ErrorCode::TOO_MANY_CARERS:
                out << "TOO_MANY_CARERS";
                break;
            case RouteValidatorBase::ErrorCode::LATE_ARRIVAL:
                out << "LATE_ARRIVAL";
                break;
            case RouteValidatorBase::ErrorCode::BREAK_VIOLATION:
                out << "BREAK_VIOLATION";
                break;
            case RouteValidatorBase::ErrorCode::ABSENT_CARER:
                out << "ABSENT_CARER";
                break;
            case RouteValidatorBase::ErrorCode::UNKNOWN:
                out << "UNKNOWN";
                break;
            case RouteValidatorBase::ErrorCode::MOVED:
                out << "MOVED";
                break;
            case RouteValidatorBase::ErrorCode::ORPHANED:
                out << "ORPHANED";
                break;
        }
        return out;
    }

    RouteValidatorBase::ValidationError::ValidationError(RouteValidatorBase::ErrorCode error_code)
            : ValidationError(error_code, {}) {}

    RouteValidatorBase::ValidationError::ValidationError(RouteValidatorBase::ErrorCode error_code,
                                                         std::string error_message)
            : error_message_(std::move(error_message)),
              error_code_(error_code) {}

    RouteValidatorBase::ErrorCode RouteValidatorBase::ValidationError::error_code() const {
        return error_code_;
    }

    const std::string &RouteValidatorBase::ValidationError::error_message() const {
        return error_message_;
    }

    void RouteValidatorBase::ValidationError::Print(std::ostream &out) const {
        out << boost::format("RouteValidationError: %1%") % error_message_;
    }

    RouteValidatorBase::RouteConflictError::RouteConflictError(const CalendarVisit &visit,
                                                               std::vector<rows::Route> routes)
            : ValidationError(RouteValidatorBase::ErrorCode::TOO_MANY_CARERS),
              visit_(visit),
              routes_(std::move(routes)) {}

    RouteValidatorBase::ScheduledVisitError::ScheduledVisitError(ErrorCode error_code,
                                                                 std::string error_message,
                                                                 const ScheduledVisit &visit,
                                                                 const rows::Route &route)
            : ValidationError(error_code, std::move(error_message)),
              visit_(visit),
              route_(route) {}

    void RouteValidatorBase::RouteConflictError::Print(std::ostream &out) const {
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

    const rows::CalendarVisit &RouteValidatorBase::RouteConflictError::visit() const {
        return visit_;
    }

    const std::vector<rows::Route> &RouteValidatorBase::RouteConflictError::routes() const {
        return routes_;
    }

    void RouteValidatorBase::ScheduledVisitError::Print(std::ostream &out) const {
        out << (boost::format("ScheduledVisitError: %1%") % error_message_).str();
    }

    const rows::ScheduledVisit &RouteValidatorBase::ScheduledVisitError::visit() const {
        return visit_;
    }

    std::vector<std::unique_ptr<rows::RouteValidatorBase::ValidationError>>
    RouteValidatorBase::ValidateAll(const std::vector<rows::Route> &routes,
                                    const rows::Problem &problem,
                                    SolverWrapper &solver) const {
        std::vector<std::unique_ptr<rows::RouteValidatorBase::ValidationError>> validation_errors;

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
            if (visit_index_pair.second.size() != visit_index_pair.first.carer_count()) {
                validation_errors.emplace_back(
                        std::make_unique<RouteConflictError>(visit_index_pair.first,
                                                             visit_index_pair.second));
            }
        }

        int route_number = 0;
        for (const auto &route : routes) {
            ++route_number;

            std::vector<ScheduledVisit> visits_to_use;
            for (const auto &visit : route.visits()) {
                if (!IsAssignedAndActive(visit)) {
                    continue;
                }

                const auto &calendar_visit = visit.calendar_visit().get();
                if (!solver.Contains(calendar_visit)) {
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(CreateOrphanedError(route, visit)));
                    continue;
                }

                if (visit.datetime() != calendar_visit.datetime()
                    || visit.duration() != calendar_visit.duration()) {
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
            const auto &diary = problem.diary(carer, visits_to_use.front().datetime().date());
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

    bool rows::RouteValidatorBase::IsAssignedAndActive(const rows::ScheduledVisit &visit) {
        return visit.calendar_visit().is_initialized()
               && visit.carer().is_initialized()
               && visit.type() == ScheduledVisit::VisitType::UNKNOWN;
    }

    RouteValidatorBase::ScheduledVisitError RouteValidatorBase::CreateAbsentCarerError(
            const rows::Route &route,
            const ScheduledVisit &visit) const {
        return {ErrorCode::ABSENT_CARER,
                (boost::format("NodeToCarer %1% is absent on the visit %2% day.")
                 % route.carer().sap_number()
                 % visit.service_user().get().id()).str(),
                visit,
                route};
    }

    RouteValidatorBase::ScheduledVisitError RouteValidatorBase::CreateLateArrivalError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            const boost::posix_time::ptime::time_duration_type delay) const {
        return {ErrorCode::LATE_ARRIVAL,
                (boost::format("NodeToCarer %1% arrives with a delay of %2% to the visit %3%.")
                 % visit.carer().get().sap_number()
                 % delay
                 % visit.service_user().get().id()).str(),
                visit,
                route};
    }

    RouteValidatorBase::ScheduledVisitError RouteValidatorBase::CreateContractualBreakViolationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit) const {
        return {ErrorCode::BREAK_VIOLATION,
                (boost::format("The visit %1% violates contractual breaks of the carer %2%.")
                 % visit.service_user().get().id()
                 % route.carer().sap_number()).str(),
                visit,
                route};
    }

    RouteValidatorBase::ScheduledVisitError RouteValidatorBase::CreateContractualBreakViolationError(
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
                    (boost::format(
                            "The visit %1% violates contractual breaks of the carer %2%: [%3%, %4%] does not fit into %5%.")
                     % visit.service_user().get().id()
                     % route.carer().sap_number()
                     % visit.datetime().time_of_day()
                     % (visit.datetime().time_of_day() + visit.duration())
                     % joined_slot_text).str(),
                    visit,
                    route};
        }
    }

    RouteValidatorBase::ScheduledVisitError RouteValidatorBase::CreateMissingInformationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            std::string error_msg) const {
        return {ErrorCode::MISSING_INFO, std::move(error_msg), visit, route};
    }

    RouteValidatorBase::ScheduledVisitError
    RouteValidatorBase::CreateOrphanedError(const Route &route, const ScheduledVisit &visit) const {
        return {ErrorCode::ORPHANED,
                (boost::format("The visit %1% is not present in the problem definition.")
                 % visit).str(),
                visit,
                route};
    }

    RouteValidatorBase::ScheduledVisitError RouteValidatorBase::CreateMovedError(
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

        return {ErrorCode::MOVED, error_msg, visit, route};
    }

    RouteValidatorBase::ValidationResult RouteValidator::Validate(const Route &route,
                                                                  SolverWrapper &solver) const {
        static const boost::posix_time::time_duration MARGIN{boost::posix_time::seconds(1)};

        boost::posix_time::time_duration total_available_time{boost::posix_time::seconds(0)};
        boost::posix_time::time_duration total_service_time{boost::posix_time::seconds(0)};
        boost::posix_time::time_duration total_travel_time{boost::posix_time::seconds(0)};

        const auto &visits = route.visits();
        if (visits.empty()) {
            return RouteValidatorBase::ValidationResult{
                    RouteValidatorBase::Metrics{total_available_time, total_service_time, total_travel_time}};
        }

        const auto first_visit_date = visits.front().datetime().date();
        const auto route_size = route.visits().size();
        for (auto visit_pos = 1u; visit_pos < route_size; ++visit_pos) {
            if (visits[visit_pos].datetime().date() != first_visit_date) {
                return RouteValidatorBase::ValidationResult{
                        std::make_unique<ValidationError>(
                                CreateValidationError("Route contains visits that span across multiple days"))};
            }
        }

        const auto diary = solver.problem().diary(route.carer(), visits.front().datetime().date());
        auto work_interval_it = std::begin(diary.get().events());
        const auto work_interval_end_it = std::end(diary.get().events());

        if (work_interval_it == work_interval_end_it) {
            return RouteValidatorBase::ValidationResult{
                    std::make_unique<ScheduledVisitError>(CreateContractualBreakViolationError(route, visits.back()))};
        }

        for (auto interval_it = work_interval_it; interval_it != work_interval_end_it; ++interval_it) {
            total_available_time += interval_it->duration();
        }

        if (VLOG_IS_ON(2)) {
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
                               return (boost::format("[%1%,%2%]")
                                       % event.begin().time_of_day()
                                       % event.end().time_of_day()).str();
                           });

            VLOG(2) << "Validating path: " << boost::algorithm::join(text_locations, ", ")
                    << " within work intervals: " << boost::algorithm::join(text_intervals, ", ");
            for (const auto &visit  : visits) {
                const auto start_time = visit.datetime().time_of_day();
                VLOG(2) << boost::format("[%1%, %2%] %3%")
                           % boost::posix_time::seconds(solver.GetBeginWindow(start_time))
                           % boost::posix_time::seconds(solver.GetEndWindow(start_time))
                           % visit.duration();
            }
        }

        {
            const auto &first_visit = visits.front();
            const auto first_visit_fixed_begin = first_visit.datetime().time_of_day();
            const boost::posix_time::time_duration first_visit_earliest_begin{boost::posix_time::seconds(
                    solver.GetBeginWindow(first_visit_fixed_begin))};
            const boost::posix_time::time_duration first_visit_latest_begin{boost::posix_time::seconds(
                    solver.GetEndWindow(first_visit_fixed_begin))};

            while (work_interval_it != work_interval_end_it
                   && util::COMP_LT(first_visit_latest_begin, work_interval_it->begin().time_of_day(), MARGIN)) {
                ++work_interval_it;
            }

            if (work_interval_it == work_interval_end_it) {
                VLOG(2) << "Cannot perform visit " << first_visit << " within assumed working hours of the carer";
                return RouteValidatorBase::ValidationResult{
                        std::make_unique<ScheduledVisitError>(
                                CreateContractualBreakViolationError(route, visits.front()))};
            }
        }

        auto last_time = work_interval_it->begin().time_of_day();
        auto last_node = solver.DEPOT;

        for (const auto &visit : visits) {
            const auto visit_node = *(solver.GetNodes(visit).begin());
            const auto travel_time = boost::posix_time::seconds(solver.Distance(last_node, visit_node));

            total_travel_time += travel_time;
            total_service_time += visit.duration();

            const boost::posix_time::time_duration earliest_service_start{
                    boost::posix_time::seconds(solver.GetBeginWindow(visit.datetime().time_of_day()))};
            const boost::posix_time::time_duration latest_service_start{
                    boost::posix_time::seconds(solver.GetEndWindow(visit.datetime().time_of_day()))};

            const boost::posix_time::time_duration arrival_time = last_time + travel_time;
            boost::posix_time::time_duration service_start = arrival_time;
            // direct travel from to the next visit would violate a break
            if (util::COMP_GT(arrival_time, work_interval_it->end().time_of_day(), MARGIN)) {
                while (work_interval_it != work_interval_end_it
                       && (util::COMP_GT(arrival_time, work_interval_it->end().time_of_day(), MARGIN)
                           //|| util::COMP_LT(arrival_time, work_interval_it->begin().time_of_day(), MARGIN) // this is wrong
                           || work_interval_it->duration().total_seconds() < travel_time.total_seconds())) {
                    ++work_interval_it;
                }

                if (work_interval_it == work_interval_end_it) {
                    VLOG(2) << "[TIME_CAPACITY_CONSTRAINT_VIOLATION] Carer does not have enough "
                            << "capacity to accommodate travel time "
                            << arrival_time
                            << " to reach next visit";
                    return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                            CreateContractualBreakViolationError(route, visits.back()))};
                }

                service_start = work_interval_it->begin().time_of_day() + travel_time;
            }

            service_start = std::max(service_start, earliest_service_start);
            if (util::COMP_GT(service_start, latest_service_start, MARGIN)) {
                VLOG(2) << "[LATEST_ARRIVAL_CONSTRAINT_VIOLATION_FIRST_STAGE] "
                        << " approached: " << visit.location().get()
                        << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                        << " travelled: " << travel_time
                        << " arrived: " << arrival_time
                        << " service_start: " << service_start
                        << " latest_service_start: : " << latest_service_start;
                return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                        CreateLateArrivalError(route, visit, service_start - latest_service_start))};
            }

            // find a slot that:
            boost::posix_time::time_duration service_finish = service_start + visit.duration();
            while (util::COMP_GT(service_finish, work_interval_it->end().time_of_day(), MARGIN)) {
                ++work_interval_it;
                if (work_interval_it == work_interval_end_it) {
                    VLOG(2) << "[BREAK_CONSTRAINT_VIOLATION_FIRST_STAGE]"
                            << " approached: " << visit.location().get()
                            << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                            << " travelled: " << travel_time
                            << " arrived: " << arrival_time
                            << " service_start: " << service_start
                            << " completed_service: " << service_finish
                            << " planned_break: " << work_interval_it->end().time_of_day();

                    return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                            CreateContractualBreakViolationError(route, visits.back()))};
                }

                service_start = work_interval_it->begin().time_of_day();
                if (util::COMP_GT(service_start, latest_service_start, MARGIN)) {
                    VLOG(2) << "[LATEST_ARRIVAL_CONSTRAINT_VIOLATION_SECOND_STAGE] "
                            << " approached: " << visit.location().get()
                            << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                            << " travelled: " << travel_time
                            << " arrived: " << arrival_time
                            << " service_start: " << service_start
                            << " latest_service_start: : " << latest_service_start;
                    return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                            CreateLateArrivalError(route, visit, service_start - latest_service_start))};
                }

                service_finish = service_start + visit.duration();
            }

            if (util::COMP_GT(service_finish, work_interval_it->end().time_of_day(), MARGIN)) {
                VLOG(2) << "[BREAK_CONSTRAINT_VIOLATION_SECOND_STAGE]"
                        << " approached: " << visit.location().get()
                        << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                        << " travelled: " << travel_time
                        << " arrived: " << arrival_time
                        << " service_start: " << service_start
                        << " completed_service: " << service_finish
                        << " planned_break: " << work_interval_it->end().time_of_day();

                return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                        CreateContractualBreakViolationError(route, visits.back()))};
            }

            VLOG(2) << "approached: " << visit.location().get()
                    << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                    << " travelled: " << travel_time
                    << " arrived: " << arrival_time
                    << " started_service: " << service_start
                    << " completed_service: " << service_finish;

            last_time = service_finish;
            last_node = visit_node;
        }

        last_time += boost::posix_time::seconds(solver.Distance(last_node, SolverWrapper::DEPOT));
        if (last_time > work_interval_it->end().time_of_day()) {
            return RouteValidatorBase::ValidationResult{
                    std::make_unique<ScheduledVisitError>(CreateContractualBreakViolationError(route, visits.back()))};
        }

        return RouteValidatorBase::ValidationResult{
                RouteValidatorBase::Metrics{total_available_time, total_service_time, total_travel_time}};
    }

    RouteValidatorBase::ValidationResult
    SimpleRouteValidator::Validate(const rows::Route &route, rows::SolverWrapper &solver) const {
        using boost::posix_time::time_duration;
        using boost::posix_time::seconds;

        static const time_duration MARGIN{seconds(1)};

        time_duration total_available_time{seconds(0)};
        time_duration total_service_time{seconds(0)};
        time_duration total_travel_time{seconds(0)};

        const auto &visits = route.visits();
        if (visits.empty()) {
            return RouteValidatorBase::ValidationResult{
                    RouteValidatorBase::Metrics{total_available_time, total_service_time, total_travel_time}};
        }

        const auto first_visit_date = visits.front().datetime().date();
        const auto route_size = route.visits().size();
        for (auto visit_pos = 1u; visit_pos < route_size; ++visit_pos) {
            if (visits[visit_pos].datetime().date() != first_visit_date) {
                return RouteValidatorBase::ValidationResult{
                        std::make_unique<ValidationError>(
                                CreateValidationError("Route contains visits that span across multiple days"))};
            }
        }

        const auto diary = solver.problem().diary(route.carer(), visits.front().datetime().date());
        auto work_interval_it = std::begin(diary.get().events());
        const auto work_interval_end_it = std::end(diary.get().events());

        if (work_interval_it == work_interval_end_it) {
            return RouteValidatorBase::ValidationResult{
                    std::make_unique<ScheduledVisitError>(CreateContractualBreakViolationError(route, visits.back()))};
        }

        for (auto interval_it = work_interval_it; interval_it != work_interval_end_it; ++interval_it) {
            total_available_time += interval_it->duration();
        }

        if (VLOG_IS_ON(2)) {
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
                               return (boost::format("[%1%,%2%]")
                                       % event.begin().time_of_day()
                                       % event.end().time_of_day()).str();
                           });

            VLOG(2) << "Validating path: " << boost::algorithm::join(text_locations, ", ")
                    << " within work intervals: " << boost::algorithm::join(text_intervals, ", ");
            for (const auto &visit  : visits) {
                const auto start_time = visit.datetime().time_of_day();
                VLOG(2) << boost::format("[%1%, %2%] %3%")
                           % seconds(solver.GetBeginWindow(start_time))
                           % seconds(solver.GetEndWindow(start_time))
                           % visit.duration();
            }
        }

        {
            const auto &first_visit = visits.front();
            const auto first_visit_fixed_begin = first_visit.datetime().time_of_day();
            const time_duration first_visit_earliest_begin{seconds(solver.GetBeginWindow(first_visit_fixed_begin))};
            const time_duration first_visit_latest_begin{seconds(solver.GetEndWindow(first_visit_fixed_begin))};

            while (work_interval_it != work_interval_end_it
                   && util::COMP_LT(first_visit_latest_begin, work_interval_it->begin().time_of_day(), MARGIN)) {
                ++work_interval_it;
            }

            if (work_interval_it == work_interval_end_it) {
                VLOG(2) << "Cannot perform visit " << first_visit << " within assumed working hours of the carer";
                return RouteValidatorBase::ValidationResult{
                        std::make_unique<ScheduledVisitError>(
                                CreateContractualBreakViolationError(route, visits.front()))};
            }
        }

        // find a slot that would let me complete a visit and move to the next place
        // if that is not possible fail

        // find intervals that suits service time and is wide enough to accommodate service and transfer to the subsequent visit
        // if no such interval is available fail

        std::vector<operations_research::RoutingModel::NodeIndex> nodes{};
        nodes.push_back(solver.DEPOT);
        for (const auto &visit:  visits) {
            nodes.push_back(*(solver.GetNodes(visit).begin()));
        }
        nodes.push_back(solver.DEPOT);

        auto last_time = work_interval_it->begin().time_of_day();
        auto next_travel_time = seconds(solver.Distance(solver.DEPOT, nodes[1]));
        last_time += next_travel_time;

        const auto visit_count = visits.size();
        for (auto visit_pos = 0; visit_pos < visit_count; ++visit_pos) {
            auto prev_node = nodes[visit_pos];
            auto current_node = nodes[visit_pos + 1];
            auto next_node = nodes[visit_pos + 2];
            auto current_travel_time = next_travel_time;
            next_travel_time = seconds(solver.Distance(current_node, next_node));

            total_travel_time += current_travel_time;

            const auto &visit = visits[visit_pos];

            const time_duration earliest_service_start{seconds(solver.GetBeginWindow(visit.datetime().time_of_day()))};
            const time_duration latest_service_start{seconds(solver.GetEndWindow(visit.datetime().time_of_day()))};

            auto service_start = std::min(std::max(last_time, earliest_service_start), latest_service_start);
            if (util::COMP_GT(last_time, latest_service_start, MARGIN)) {
                VLOG(2) << "[LATEST_ARRIVAL_CONSTRAINT_VIOLATION_FIRST_STAGE] "
                        << " approached: " << visit.location().get()
                        << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                        << " travelled: " << current_travel_time
                        << " arrived: " << last_time
                        << " service_start: " << service_start
                        << " latest_service_start: : " << latest_service_start;
                return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                        CreateLateArrivalError(route, visit, service_start - latest_service_start))};
            }

            // find a slot that:
            time_duration completed_service_and_travel_to_next = service_start + visit.duration() + next_travel_time;
            while (util::COMP_GT(completed_service_and_travel_to_next, work_interval_it->end().time_of_day(), MARGIN)) {
                ++work_interval_it;
                if (work_interval_it == work_interval_end_it) {
                    VLOG(2) << "[BREAK_CONSTRAINT_VIOLATION_FIRST_STAGE]"
                            << " approached: " << visit.location().get()
                            << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                            << " travelled: " << current_travel_time
                            << " arrived: " << last_time
                            << " service_start: " << service_start
                            << " completed_service_and_travel_to_next: " << completed_service_and_travel_to_next
                            << " planned_break: " << work_interval_it->end().time_of_day();

                    return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                            CreateContractualBreakViolationError(route, visits.back()))};
                }

                service_start = work_interval_it->begin().time_of_day();
                if (util::COMP_GT(service_start, latest_service_start, MARGIN)) {
                    VLOG(2) << "[LATEST_ARRIVAL_CONSTRAINT_VIOLATION_SECOND_STAGE] "
                            << " approached: " << visit.location().get()
                            << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                            << " travelled: " << current_travel_time
                            << " arrived: " << last_time
                            << " completed_service_and_travel_to_next: " << service_start
                            << " latest_service_start: : " << latest_service_start;
                    return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                            CreateLateArrivalError(route, visit, service_start - latest_service_start))};
                }

                completed_service_and_travel_to_next = service_start + visit.duration() + next_travel_time;
            }

            if (util::COMP_GT(completed_service_and_travel_to_next, work_interval_it->end().time_of_day(), MARGIN)) {
                VLOG(2) << "[BREAK_CONSTRAINT_VIOLATION_SECOND_STAGE]"
                        << " approached: " << visit.location().get()
                        << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                        << " travelled: " << current_travel_time
                        << " arrived: " << last_time
                        << " service_start: " << service_start
                        << " completed_service_and_travel_to_next: " << completed_service_and_travel_to_next
                        << " planned_break: " << work_interval_it->end().time_of_day();

                return RouteValidatorBase::ValidationResult{std::make_unique<ScheduledVisitError>(
                        CreateContractualBreakViolationError(route, visits.back()))};
            }

            VLOG(2) << "approached: " << visit.location().get()
                    << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                    << " travelled: " << current_travel_time
                    << " arrived: " << last_time
                    << " started_service: " << service_start
                    << " completed_service_and_travel_to_next: " << completed_service_and_travel_to_next;

            last_time = completed_service_and_travel_to_next;
        }

        if (last_time > work_interval_it->end().time_of_day()) {
            return RouteValidatorBase::ValidationResult{
                    std::make_unique<ScheduledVisitError>(CreateContractualBreakViolationError(route, visits.back()))};
        }

        return RouteValidatorBase::ValidationResult{
                RouteValidatorBase::Metrics{total_available_time, total_service_time, total_travel_time}};
    }

    const boost::posix_time::time_duration SimpleRouteValidatorWithTimeWindows::MARGIN
            = static_cast<boost::posix_time::time_duration>(boost::posix_time::seconds(1));

    RouteValidatorBase::ValidationResult
    SimpleRouteValidatorWithTimeWindows::Validate(const rows::Route &route, rows::SolverWrapper &solver) const {
        using boost::posix_time::time_duration;
        using boost::posix_time::seconds;

        Session session{route, solver, *this};
        session.Initialize();

        while (session.HasMoreVisits()) {
            const auto &visit = session.GetCurrentVisit();

            if (session.HasMoreBreaks()) {
                const auto &break_interval = session.GetCurrentBreak();
                const auto fastest_break_finish = session.GetExpectedFinish(session.GetCurrentBreak());
                const auto fastest_visit_finish = session.GetExpectedFinish(visit);

                if (session.StartsAfter(fastest_break_finish, visit)
                    || !session.CanPerformAfter(fastest_visit_finish, break_interval)
                    || session.CanPerformAfter(fastest_break_finish, visit)) {
                    session.Perform(break_interval);
                    continue;
                }
            }

            session.Perform(session.GetCurrentVisit());
        }

        while (session.HasMoreBreaks()) {
            session.Perform(session.GetCurrentBreak());
        }

        return session.ToValidationResult();
    }

    RouteValidatorBase::ValidationError RouteValidatorBase::CreateValidationError(std::string error_msg) const {
        return RouteValidatorBase::ValidationError{ErrorCode::UNKNOWN, std::move(error_msg)};
    }

    RouteValidatorBase::ValidationResult::ValidationResult()
            : metrics_{},
              error_{nullptr} {}

    RouteValidatorBase::ValidationResult::ValidationResult(RouteValidatorBase::Metrics metrics)
            : metrics_{std::move(metrics)},
              error_{nullptr} {}

    RouteValidatorBase::ValidationResult::ValidationResult(
            std::unique_ptr<RouteValidatorBase::ValidationError> &&error) noexcept
            : metrics_{},
              error_{std::move(error)} {}

    RouteValidatorBase::ValidationResult::ValidationResult(RouteValidatorBase::ValidationResult &&other) noexcept
            : metrics_{std::move(other.metrics_)},
              error_{std::move(other.error_)} {}

    std::unique_ptr<RouteValidatorBase::ValidationError> &RouteValidatorBase::ValidationResult::error() {
        return error_;
    }

    const std::unique_ptr<RouteValidatorBase::ValidationError> &RouteValidatorBase::ValidationResult::error() const {
        return error_;
    }

    RouteValidatorBase::ValidationResult &RouteValidatorBase::ValidationResult::operator=(
            RouteValidatorBase::ValidationResult &&other) noexcept {
        metrics_ = std::move(other.metrics_);
        error_ = std::move(other.error_);
        return *this;
    }

    const RouteValidatorBase::Metrics &RouteValidatorBase::ValidationResult::metrics() const {
        return metrics_;
    }

    RouteValidatorBase::Metrics::Metrics()
            : Metrics({}, {}, {}) {}

    RouteValidatorBase::Metrics::Metrics(boost::posix_time::time_duration available_time,
                                         boost::posix_time::time_duration service_time,
                                         boost::posix_time::time_duration travel_time)
            : available_time_(std::move(available_time)),
              service_time_(std::move(service_time)),
              travel_time_(std::move(travel_time)) {}

    RouteValidatorBase::Metrics::Metrics(const RouteValidatorBase::Metrics &metrics)
            : available_time_(metrics.available_time_),
              service_time_(metrics.service_time_),
              travel_time_(metrics.travel_time_) {}

    RouteValidatorBase::Metrics::Metrics(RouteValidatorBase::Metrics &&metrics) noexcept
            : available_time_(std::move(metrics.available_time_)),
              service_time_(std::move(metrics.service_time_)),
              travel_time_(std::move(metrics.travel_time_)) {}

    RouteValidatorBase::Metrics &RouteValidatorBase::Metrics::operator=(const RouteValidatorBase::Metrics &metrics) {
        available_time_ = metrics.available_time_;
        service_time_ = metrics.service_time_;
        travel_time_ = metrics.travel_time_;
        return *this;
    }

    RouteValidatorBase::Metrics &
    RouteValidatorBase::Metrics::operator=(RouteValidatorBase::Metrics &&metrics) noexcept {
        available_time_ = std::move(metrics.available_time_);
        service_time_ = std::move(metrics.service_time_);
        travel_time_ = std::move(metrics.travel_time_);
        return *this;
    }

    boost::posix_time::time_duration RouteValidatorBase::Metrics::available_time() const {
        return available_time_;
    }

    boost::posix_time::time_duration RouteValidatorBase::Metrics::service_time() const {
        return service_time_;
    }

    boost::posix_time::time_duration RouteValidatorBase::Metrics::travel_time() const {
        return travel_time_;
    }

    boost::posix_time::time_duration RouteValidatorBase::Metrics::idle_time() const {
        return available_time_ - service_time_ - travel_time_;
    }

    bool SimpleRouteValidatorWithTimeWindows::Session::HasMoreVisits() const {
        if (error_) {
            return false;
        }
        return current_visit_ < visits_.size();
    }

    bool SimpleRouteValidatorWithTimeWindows::Session::HasMoreBreaks() const {
        if (error_) {
            return false;
        }
        return current_break_ < breaks_.size();
    }

    const ScheduledVisit &SimpleRouteValidatorWithTimeWindows::Session::GetCurrentVisit() const {
        return visits_[current_visit_];
    }

    const Event &SimpleRouteValidatorWithTimeWindows::Session::GetCurrentBreak() const {
        return breaks_[current_break_];
    }

    boost::posix_time::time_duration SimpleRouteValidatorWithTimeWindows::Session::GetBeginWindow(
            const Event &interval) const {
        if (!breaks_.empty() && breaks_.front() != interval && breaks_.back() != interval) {
            return boost::posix_time::seconds(solver_.GetBeginWindow(interval.begin().time_of_day()));
        }

        return interval.begin().time_of_day();
    }

    boost::posix_time::time_duration SimpleRouteValidatorWithTimeWindows::Session::GetEndWindow(
            const Event &interval) const {
        if (!breaks_.empty() && breaks_.front() != interval && breaks_.back() != interval) {
            return boost::posix_time::seconds(solver_.GetEndWindow(interval.begin().time_of_day()));
        }

        return interval.begin().time_of_day();
    }

    boost::posix_time::time_duration
    SimpleRouteValidatorWithTimeWindows::Session::GetBeginWindow(const ScheduledVisit &visit) const {
        return boost::posix_time::seconds(solver_.GetBeginWindow(visit.datetime().time_of_day()));
    }

    boost::posix_time::time_duration
    SimpleRouteValidatorWithTimeWindows::Session::GetEndWindow(const ScheduledVisit &visit) const {
        return boost::posix_time::seconds(solver_.GetEndWindow(visit.datetime().time_of_day()));
    }

    SimpleRouteValidatorWithTimeWindows::Session::Session(const Route &route,
                                                          SolverWrapper &solver,
                                                          const RouteValidatorBase &validator)
            : route_(route),
              solver_(solver),
              validator_(validator),
              total_available_time_(),
              total_service_time_(),
              total_travel_time_(),
              error_(nullptr),
              visits_(),
              breaks_(),
              current_time_() {}

    void SimpleRouteValidatorWithTimeWindows::Session::Initialize() {
        using boost::posix_time::seconds;
        using boost::posix_time::time_duration;

        const auto &visits = route_.visits();
        if (visits.empty()) {
            return;
        }

        const auto first_visit_date = visits.front().datetime().date();
        const auto route_size = visits.size();
        for (auto visit_pos = 1u; visit_pos < route_size; ++visit_pos) {
            if (visits[visit_pos].datetime().date() != first_visit_date) {
                error_ = std::make_unique<ValidationError>(
                        validator_.CreateValidationError("Route contains visits that span across multiple days"));
                return;
            }
        }

        const auto diary = solver_.problem().diary(route_.carer(), first_visit_date);
        if (!diary.is_initialized()) {
            error_ = std::make_unique<ValidationError>(
                    validator_.CreateValidationError((boost::format("Carer %1% is absent on %2%")
                                                      % route_.carer()
                                                      % first_visit_date).str()));
            return;
        }

        for (const auto &event : diary.get().events()) {
            total_available_time_ += event.duration();
        }

        auto work_interval_it = std::begin(diary.get().events());
        const auto work_interval_end_it = std::end(diary.get().events());
        if (work_interval_it == work_interval_end_it) {
            error_ = std::make_unique<ScheduledVisitError>(
                    validator_.CreateContractualBreakViolationError(route_, visits.back()));
            return;
        }

        visits_ = route_.visits();
        last_node_ = solver_.DEPOT;
        current_visit_ = 0;

        current_node_ = solver_.DEPOT;
        if (!visits_.empty()) {
            current_node_ = GetNode(visits_.front());
        }

        next_node_ = solver_.DEPOT;
        if (visits.size() > 1) {
            next_node_ = GetNode(visits_[1]);
        }

        breaks_ = diary.get().Breaks();
        current_break_ = 0;

        if (VLOG_IS_ON(2)) {
            VLOG(2) << boost::format("Validating path %1%")
                       % route_.carer();

            for (const auto &visit  : visits) {
                VLOG(2) << boost::format("%|5s| [%s, %s] %s")
                           % GetNode(visit)
                           % GetBeginWindow(visit)
                           % GetEndWindow(visit)
                           % visit.duration();
            }

            for (const auto &break_interval : breaks_) {
                VLOG(2) << boost::format("[%1%, %2%] %3%")
                           % GetBeginWindow(break_interval)
                           % GetEndWindow(break_interval)
                           % break_interval.duration();
            }
        }

        current_time_ = boost::posix_time::hours(24);
        if (!breaks_.empty()) {
            current_time_ = GetBeginWindow(breaks_.front());
        }

        if (!visits_.empty()) {
            current_time_ = std::min(current_time_, GetBeginWindow(visits_.front()));
        }
    }

    bool SimpleRouteValidatorWithTimeWindows::Session::GreaterThan(const boost::posix_time::time_duration &left,
                                                                   const boost::posix_time::time_duration &right) const {
        return util::COMP_GT(left, right, MARGIN);
    }

    bool SimpleRouteValidatorWithTimeWindows::Session::GreaterEqual(const boost::posix_time::time_duration &left,
                                                                    const boost::posix_time::time_duration &right) const {
        return util::COMP_GT(left, right, MARGIN)
               || (abs(left.total_seconds() - right.total_seconds()) <= MARGIN.total_seconds());
    }

    void SimpleRouteValidatorWithTimeWindows::Session::Perform(const ScheduledVisit &visit) {
        using boost::posix_time::time_duration;

        const time_duration earliest_service_start{GetBeginWindow(visit)};
        const time_duration latest_service_start{GetEndWindow(visit)};

        const auto travel_time = GetTravelTime(last_node_, current_node_);
        const auto arrival_time = current_time_ + travel_time;
        const auto &service_start = std::max(arrival_time, earliest_service_start);
        if (GreaterThan(service_start, latest_service_start)) {
            VLOG(2) << "[LATEST_ARRIVAL_CONSTRAINT_VIOLATION_SECOND_STAGE] "
                    << " approached: " << visit.location().get()
                    << " [ " << earliest_service_start << "," << latest_service_start << " ]"
                    << " travelled: " << travel_time
                    << " arrived: " << arrival_time
                    << " service_start: " << service_start
                    << " latest_service_start: : " << latest_service_start;
            error_ = std::make_unique<ScheduledVisitError>(
                    validator_.CreateLateArrivalError(route_, visit, service_start - latest_service_start));
            return;
        }

        VLOG(2) << boost::format("[%1%, %2%] travel_time: %3% arrival: %4% service_start: %5%")
                   % earliest_service_start
                   % latest_service_start
                   % travel_time
                   % arrival_time
                   % service_start;

        last_node_ = current_node_;
        current_node_ = next_node_;

        ++current_visit_;
        if (current_visit_ + 1 < visits_.size()) {
            next_node_ = GetNode(visits_[current_visit_ + 1]);
        } else {
            next_node_ = solver_.DEPOT;
        }

        current_time_ = service_start + visit.duration();
        total_travel_time_ += travel_time;
        total_service_time_ += visit.duration();
    }

    boost::posix_time::time_duration
    SimpleRouteValidatorWithTimeWindows::Session::GetExpectedFinish(const ScheduledVisit &visit) const {
        // deliberately increase estimation of the expected finish, so necessary travel to the subsequent destination
        // takes place before before a break

        const auto arrival_time = current_time_ + GetTravelTime(last_node_, current_node_);
        const auto &service_start = std::max(arrival_time, GetBeginWindow(visit));
        return service_start + visit.duration() + GetTravelTime(current_node_, next_node_);
    }

    void SimpleRouteValidatorWithTimeWindows::Session::Perform(const Event &interval) {
        using boost::posix_time::time_duration;

        const time_duration earliest_break_start{GetBeginWindow(interval)};
        const time_duration latest_break_start{GetEndWindow(interval)};

        const auto &break_start = std::max(earliest_break_start, current_time_);
        if (GreaterThan(break_start, latest_break_start)) {
            std::stringstream stream_msg;
            stream_msg << "[BREAK_CONSTRAINT_VIOLATION]"
                       << " [ " << earliest_break_start << "," << latest_break_start << " ]"
                       << " break_start: " << break_start;

            VLOG(2) << stream_msg.rdbuf();

            error_ = std::make_unique<ScheduledVisitError>(
                    validator_.CreateContractualBreakViolationError(route_, visits_.front()));
            return;
        }

        VLOG(2) << boost::format("[%1%, %2%] start: %3% duration: %4%")
                   % earliest_break_start
                   % latest_break_start
                   % break_start
                   % interval.duration();

        current_time_ = break_start + interval.duration();
        ++current_break_;
    }

    boost::posix_time::time_duration
    SimpleRouteValidatorWithTimeWindows::Session::GetExpectedFinish(const Event &interval) const {
        const auto &break_start = std::max(GetBeginWindow(interval), current_time_);
        return break_start + interval.duration();
    }

    bool SimpleRouteValidatorWithTimeWindows::Session::StartsAfter(boost::posix_time::time_duration time_of_day,
                                                                   const ScheduledVisit &visit) const {
        return GreaterEqual(GetBeginWindow(visit), time_of_day + GetTravelTime(last_node_, current_node_));
    }

    bool SimpleRouteValidatorWithTimeWindows::Session::CanPerformAfter(boost::posix_time::time_duration time_of_day,
                                                                       const Event &break_interval) const {
        return GreaterEqual(GetEndWindow(break_interval), time_of_day);
    }

    bool SimpleRouteValidatorWithTimeWindows::Session::CanPerformAfter(boost::posix_time::time_duration time_of_day,
                                                                       const ScheduledVisit &visit) const {
        return GreaterEqual(GetEndWindow(visit), time_of_day + GetTravelTime(last_node_, current_node_));
    }

    RouteValidatorBase::ValidationResult SimpleRouteValidatorWithTimeWindows::Session::ToValidationResult() {
        if (error_) {
            return ValidationResult(std::move(error_));
        }

        return RouteValidatorBase::ValidationResult{
                RouteValidatorBase::Metrics{total_available_time_,
                                            total_service_time_,
                                            total_travel_time_}};
    }

    boost::posix_time::time_duration SimpleRouteValidatorWithTimeWindows::Session::GetTravelTime(
            operations_research::RoutingModel::NodeIndex from_node,
            operations_research::RoutingModel::NodeIndex to_node) const {
        return boost::posix_time::seconds(solver_.Distance(from_node, to_node));
    }

    operations_research::RoutingModel::NodeIndex
    SimpleRouteValidatorWithTimeWindows::Session::GetNode(const ScheduledVisit &visit) const {
        return *std::cbegin(solver_.GetNodes(visit));
    }
}
