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
                    validation_errors.emplace_back(std::make_unique<ScheduledVisitError>(
                            ValidationSession::CreateMissingInformationError(
                                    route,
                                    visit,
                                    "calendar visit is missing")));
                } else if (!visit.location().is_initialized()) {
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(ValidationSession::CreateMissingInformationError(
                                    route,
                                    visit,
                                    "location is missing")));
                }
            }
        }

        // find visit assignment conflicts
        std::unordered_map<rows::CalendarVisit, std::vector<std::pair<rows::ScheduledVisit, rows::Route> > > visit_index;

        for (const auto &route: routes) {
            for (const auto &visit : route.visits()) {
                if (!IsAssignedAndActive(visit)) {
                    continue;
                }

                const auto &calendar_visit = visit.calendar_visit();
                const auto visit_index_it = visit_index.find(calendar_visit.get());
                if (visit_index_it != std::end(visit_index)) {
                    visit_index_it->second.emplace_back(visit, route);
                } else {
                    visit_index.insert({calendar_visit.get(),
                                        std::vector<std::pair<rows::ScheduledVisit, rows::Route>> {
                                                std::make_pair(visit, route)}
                                       });
                }
            }
        }

        for (const auto &visit_index_pair : visit_index) {
            if (visit_index_pair.second.size() != visit_index_pair.first.carer_count()) {
                DCHECK(!visit_index_pair.second.empty());

                std::vector<rows::Route> conflict_routes{visit_index_pair.second.front().second};
                validation_errors.emplace_back(
                        std::make_unique<RouteConflictError>(visit_index_pair.first, std::move(conflict_routes)));
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
                            std::make_unique<ScheduledVisitError>(
                                    ValidationSession::CreateOrphanedError(route, visit)));
                    continue;
                }

                if (visit.datetime() != calendar_visit.datetime()
                    || visit.duration() != calendar_visit.duration()) {
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(ValidationSession::CreateMovedError(route, visit)));
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
                            std::make_unique<ScheduledVisitError>(
                                    ValidationSession::CreateAbsentCarerError(route, visit)));
                }

                continue;
            }

            auto event_it = std::begin(diary.get().events());
            const auto event_it_end = std::end(diary.get().events());
            if (event_it == event_it_end) {
                for (const auto &visit : visits_to_use) {
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(
                                    ValidationSession::CreateAbsentCarerError(route, visit)));
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

        for (const auto &visit_bundle : visit_index) {
            if (visit_bundle.second.size() <= 1) {
                continue;
            }

            // FIXME: validation that the latest arrival is suitable for all visits seems too expensive
//            auto contains_unassigned_visit = false;
//            for (const auto &visit_route_pair : visit_bundle.second) {
//                if (!visit_route_pair.first.carer()) {
//                    contains_unassigned_visit = true;
//                    break;
//                }
//            }

//            if (contains_unassigned_visit) {
            for (const auto &visit_route_pair : visit_bundle.second) {
                if (visit_route_pair.first.carer()) {
                    validation_errors.emplace_back(
                            std::make_unique<ScheduledVisitError>(
                                    ValidationSession::NotEnoughCarersAvailable(visit_route_pair.second,
                                                                                visit_route_pair.first)));
                }
            }
//            }
        }

        return validation_errors;
    }

    bool rows::RouteValidatorBase::IsAssignedAndActive(const rows::ScheduledVisit &visit) {
        return visit.calendar_visit().is_initialized()
               && visit.carer().is_initialized()
               && visit.type() == ScheduledVisit::VisitType::UNKNOWN;
    }

    RouteValidatorBase::ScheduledVisitError ValidationSession::CreateAbsentCarerError(
            const rows::Route &route,
            const ScheduledVisit &visit) {
        return {RouteValidatorBase::ErrorCode::ABSENT_CARER,
                (boost::format("NodeToCarer %1% is absent on the visit %2% day.")
                 % route.carer().sap_number()
                 % visit.service_user().get().id()).str(),
                visit,
                route};
    }

    RouteValidatorBase::ScheduledVisitError ValidationSession::CreateLateArrivalError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            const boost::posix_time::ptime::time_duration_type delay) {
        return {RouteValidatorBase::ErrorCode::LATE_ARRIVAL,
                (boost::format("NodeToCarer %1% arrives with a delay of %2% to the visit %3%.")
                 % visit.carer().get().sap_number()
                 % delay
                 % visit.service_user().get().id()).str(),
                visit,
                route};
    }

    RouteValidatorBase::ScheduledVisitError ValidationSession::CreateContractualBreakViolationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit) {
        return {RouteValidatorBase::ErrorCode::BREAK_VIOLATION,
                (boost::format("The visit %1% violates contractual breaks of the carer %2%.")
                 % visit.service_user().get().id()
                 % route.carer().sap_number()).str(),
                visit,
                route};
    }

    RouteValidatorBase::ScheduledVisitError ValidationSession::CreateContractualBreakViolationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            std::vector<rows::Event> overlapping_slots) {
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

            return {RouteValidatorBase::ErrorCode::BREAK_VIOLATION,
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

    RouteValidatorBase::ScheduledVisitError ValidationSession::CreateMissingInformationError(
            const rows::Route &route,
            const rows::ScheduledVisit &visit,
            std::string error_msg) {
        return {RouteValidatorBase::ErrorCode::MISSING_INFO, std::move(error_msg), visit, route};
    }

    RouteValidatorBase::ScheduledVisitError
    ValidationSession::CreateOrphanedError(const Route &route, const ScheduledVisit &visit) {
        return {RouteValidatorBase::ErrorCode::ORPHANED,
                (boost::format("The visit %1% is not present in the problem definition.")
                 % visit).str(),
                visit,
                route};
    }

    RouteValidatorBase::ScheduledVisitError ValidationSession::CreateMovedError(
            const Route &route,
            const ScheduledVisit &visit) {
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

        return {RouteValidatorBase::ErrorCode::MOVED, error_msg, visit, route};
    }

    RouteValidatorBase::ScheduledVisitError ValidationSession::NotEnoughCarersAvailable(const Route &route,
                                                                                        const ScheduledVisit &visit) {
        std::string error_msg = (boost::format("Not enough carers booked for the visit %1%")
                                 % visit).str();
        return {RouteValidatorBase::ErrorCode::NOT_ENOUGH_CARERS, std::move(error_msg), visit, route};
    }

    const boost::posix_time::time_duration ValidationSession::ERROR_MARGIN
            = static_cast<boost::posix_time::time_duration>(boost::posix_time::seconds(1));

    RouteValidatorBase::ValidationResult
    SimpleRouteValidatorWithTimeWindows::Validate(const rows::Route &route, rows::SolverWrapper &solver) const {
        using boost::posix_time::time_duration;
        using boost::posix_time::seconds;

        ValidationSession session{route, solver};
        session.Initialize();

        while (session.HasMoreVisits()) {
            const auto &visit = session.GetCurrentVisit();

            if (session.HasMoreBreaks()) {
                const auto &break_interval = session.GetCurrentBreak();
                const auto fastest_break_finish = session.GetExpectedFinish(session.GetCurrentBreak());
                const auto fastest_visit_finish = session.GetExpectedFinish(visit);

                VLOG(2) << boost::format("Expected finish break: %1% Expected finish visit: %2%")
                           % fastest_break_finish
                           % fastest_visit_finish;

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

    RouteValidatorBase::ValidationError ValidationSession::CreateValidationError(std::string error_msg) {
        return RouteValidatorBase::ValidationError{RouteValidatorBase::ErrorCode::UNKNOWN, std::move(error_msg)};
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

    bool ValidationSession::HasMoreVisits() const {
        if (error_) {
            return false;
        }
        return current_visit_ < visits_.size();
    }

    bool ValidationSession::HasMoreBreaks() const {
        if (error_) {
            return false;
        }
        return current_break_ < breaks_.size();
    }

    const ScheduledVisit &ValidationSession::GetCurrentVisit() const {
        return visits_[current_visit_];
    }

    const Event &ValidationSession::GetCurrentBreak() const {
        return breaks_[current_break_];
    }

    boost::posix_time::time_duration ValidationSession::GetBeginWindow(
            const Event &interval) const {
        if (!breaks_.empty() && breaks_.front() != interval && breaks_.back() != interval) {
            return boost::posix_time::seconds(solver_.GetBeginWindow(interval.begin().time_of_day()));
        }

        return interval.begin().time_of_day();
    }

    boost::posix_time::time_duration ValidationSession::GetEndWindow(
            const Event &interval) const {
        if (!breaks_.empty() && breaks_.front() != interval && breaks_.back() != interval) {
            return boost::posix_time::seconds(solver_.GetEndWindow(interval.begin().time_of_day()));
        }

        return interval.begin().time_of_day();
    }

    boost::posix_time::time_duration ValidationSession::GetBeginWindow(const ScheduledVisit &visit) const {
        return boost::posix_time::seconds(solver_.GetBeginWindow(visit.datetime().time_of_day()));
    }

    boost::posix_time::time_duration ValidationSession::GetEndWindow(const ScheduledVisit &visit) const {
        return boost::posix_time::seconds(solver_.GetEndWindow(visit.datetime().time_of_day()));
    }

    ValidationSession::ValidationSession(const Route &route, SolverWrapper &solver)
            : route_(route),
              solver_(solver),
              total_available_time_(),
              total_service_time_(),
              total_travel_time_(),
              error_(nullptr),
              visits_(),
              nodes_(),
              breaks_(),
              current_time_() {}

    void ValidationSession::Initialize() {
        using boost::posix_time::seconds;
        using boost::posix_time::time_duration;

        visits_ = route_.visits();
        if (visits_.empty()) {
            return;
        }

        const auto first_visit_date = visits_.front().datetime().date();
        const auto route_size = visits_.size();
        for (auto visit_pos = 1u; visit_pos < route_size; ++visit_pos) {
            if (visits_[visit_pos].datetime().date() != first_visit_date) {
                error_ = std::make_unique<RouteValidatorBase::ValidationError>(
                        CreateValidationError("Route contains visits that span across multiple days"));
                return;
            }
        }

        const auto diary = solver_.problem().diary(route_.carer(), first_visit_date);
        if (!diary.is_initialized()) {
            error_ = std::make_unique<RouteValidatorBase::ValidationError>(
                    CreateValidationError((boost::format("Carer %1% is absent on %2%")
                                           % route_.carer()
                                           % first_visit_date).str()));
            return;
        }

        auto work_interval_it = std::begin(diary.get().events());
        const auto work_interval_end_it = std::end(diary.get().events());
        if (work_interval_it == work_interval_end_it) {
            error_ = std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                    CreateContractualBreakViolationError(route_, visits_.back()));
            return;
        }

        nodes_.push_back(solver_.DEPOT);
        for (const auto &visit : visits_) {
            nodes_.push_back(GetNode(visit));
        }
        nodes_.push_back(solver_.DEPOT);

        last_node_ = nodes_.front();
        current_visit_ = 0;
        current_node_ = nodes_[1];
        next_node_ = solver_.DEPOT;
        if (nodes_.size() > 2) {
            next_node_ = nodes_[2];
        }

        breaks_ = diary.get().Breaks();
        current_break_ = 0;

        if (VLOG_IS_ON(2)) {
            VLOG(2) << boost::format("Validating path %1%")
                       % route_.carer();

            for (const auto &visit  : visits_) {
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

        for (const auto &event : diary.get().events()) {
            total_available_time_ += event.duration();
        }

        auto last_node = solver_.DEPOT;
        for (auto node_pos = 1; node_pos < nodes_.size(); ++node_pos) {
            auto current_node = nodes_[node_pos];
            total_travel_time_ += boost::posix_time::seconds(solver_.Distance(last_node, current_node));
            last_node = current_node;
        }

        for (const auto &visit : visits_) {
            total_service_time_ += visit.duration();
        }
    }

    bool ValidationSession::GreaterThan(const boost::posix_time::time_duration &left,
                                        const boost::posix_time::time_duration &right) {
        return util::COMP_GT(left, right, ERROR_MARGIN);
    }

    bool ValidationSession::GreaterEqual(const boost::posix_time::time_duration &left,
                                         const boost::posix_time::time_duration &right) {
        return util::COMP_GT(left, right, ERROR_MARGIN)
               || (abs(left.total_seconds() - right.total_seconds()) <= ERROR_MARGIN.total_seconds());
    }

    void ValidationSession::Perform(const ScheduledVisit &visit) {
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
            error_ = std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                    CreateLateArrivalError(route_, visit, service_start - latest_service_start));
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
    }

    boost::posix_time::time_duration ValidationSession::GetExpectedFinish(const ScheduledVisit &visit) const {
        // deliberately increase estimation of the expected finish, so necessary travel to the subsequent destination
        // takes place before before a break

        const auto arrival_time = current_time_ + GetTravelTime(last_node_, current_node_);
        const auto visit_begin_window = GetBeginWindow(visit);
        const auto &service_start = std::max(arrival_time, visit_begin_window);
        return service_start + visit.duration() + GetTravelTime(current_node_, next_node_);
    }

    void ValidationSession::Perform(const Event &interval) {
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

            error_ = std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                    CreateContractualBreakViolationError(route_, visits_.front()));
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

    boost::posix_time::time_duration ValidationSession::GetExpectedFinish(const Event &interval) const {
        const auto begin_window = GetBeginWindow(interval);
        const auto &break_start = std::max(begin_window, current_time_);
        VLOG(2) << boost::format("Expected break finish estimation: %1% from begin window: %2% and current time: %3%")
                   % break_start
                   % GetBeginWindow(interval)
                   % current_time_;
        return break_start + interval.duration();
    }

    bool ValidationSession::StartsAfter(boost::posix_time::time_duration time_of_day,
                                        const ScheduledVisit &visit) const {
        return GreaterEqual(GetBeginWindow(visit), time_of_day + GetTravelTime(last_node_, current_node_));
    }

    bool ValidationSession::CanPerformAfter(boost::posix_time::time_duration time_of_day,
                                            const Event &break_interval) const {
        return GreaterEqual(GetEndWindow(break_interval), time_of_day);
    }

    bool ValidationSession::CanPerformAfter(boost::posix_time::time_duration time_of_day,
                                            const ScheduledVisit &visit) const {
        return GreaterEqual(GetEndWindow(visit), time_of_day + GetTravelTime(last_node_, current_node_));
    }

    RouteValidatorBase::ValidationResult ValidationSession::ToValidationResult() {
        if (error_) {
            return RouteValidatorBase::ValidationResult(std::move(error_));
        }

        return RouteValidatorBase::ValidationResult{
                RouteValidatorBase::Metrics{total_available_time_,
                                            total_service_time_,
                                            total_travel_time_}};
    }

    boost::posix_time::time_duration ValidationSession::GetTravelTime(
            operations_research::RoutingModel::NodeIndex from_node,
            operations_research::RoutingModel::NodeIndex to_node) const {
        return boost::posix_time::seconds(solver_.Distance(from_node, to_node));
    }

    operations_research::RoutingModel::NodeIndex ValidationSession::GetNode(const ScheduledVisit &visit) const {
        return *std::cbegin(solver_.GetNodes(visit));
    }

    bool ValidationSession::error() const {
        return bool(error_);
    }

    RouteValidatorBase::ValidationResult SolutionValidator::Validate(int vehicle,
                                                                     const operations_research::Assignment &solution,
                                                                     const operations_research::RoutingModel &model,
                                                                     SolverWrapper &solver) const {
        using boost::posix_time::seconds;
        using boost::posix_time::ptime;
        using boost::posix_time::time_period;

        const auto &carer = solver.Carer(vehicle);

        auto current_index = model.Start(vehicle);
        std::vector<int64> indices;
        indices.push_back(current_index);
        while (!model.IsEnd(current_index)) {
            current_index = solution.Value(model.NextVar(current_index));
            indices.push_back(current_index);
        }

        std::vector<ScheduledVisit> visits;
        for (auto node_pos = 1; node_pos < indices.size() - 1; ++node_pos) {
            const auto node_index = indices[node_pos];
            visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN,
                                carer,
                                solver.NodeToVisit(model.IndexToNode(node_index)));
        }

        Route route{carer, visits};
        ValidationSession session{route, solver};
        session.Initialize();

        if (session.error()) {
            return session.ToValidationResult();
        }

        const auto date = visits.front().datetime().date();
        const auto diary = solver.problem().diary(carer, date).get();

        // build intervals where carer does not work - it must account for travel time
        std::vector<boost::posix_time::time_period> idle_periods;

        const auto &time_dim = model.GetDimensionOrDie(SolverWrapper::TIME_DIMENSION);

        auto last_node = solver.DEPOT;
        ptime last_time = ptime(date, seconds(0));
        for (auto node_pos = 1; node_pos < indices.size() - 1; ++node_pos) {
            const auto visit_index = indices[node_pos];
            const auto visit_node = model.IndexToNode(visit_index);
            const auto &visit = visits[node_pos - 1];
            const ptime fastest_arrival{date, seconds(solver.GetBeginWindow(visit.datetime().time_of_day()))};
            const ptime latest_arrival{date, seconds(solver.GetEndWindow(visit.datetime().time_of_day()))};
            const ptime arrival{date, seconds(solution.Value(time_dim.CumulVar(visit_index)))};

            VLOG(2) << boost::format("Visit [%1%,%2%] arrival: %3% busy until %4%")
                       % fastest_arrival
                       % latest_arrival
                       % arrival
                       % (arrival + visit.duration());

            const time_period arrival_period{fastest_arrival, latest_arrival};
            if (ValidationSession::GreaterThan(arrival.time_of_day(), arrival_period.end().time_of_day())) {
                const auto arrival_delay = arrival - arrival_period.end();
                LOG(FATAL) << boost::format("Arrival time %1% is expected to be outside the interval %2%")
                              % arrival
                              % arrival_period;
                return RouteValidatorBase::ValidationResult(std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                        session.CreateLateArrivalError(route, visit, arrival_delay)));
            }

            if (ValidationSession::GreaterThan(arrival.time_of_day(), last_time.time_of_day())) {
                idle_periods.emplace_back(last_time, arrival);
            }

            const auto next_node = model.IndexToNode(indices[node_pos + 1]);
            last_time = arrival
                        + visit.duration()
                        + session.GetTravelTime(visit_node, next_node);
        }

        ptime end_of_day(date, boost::posix_time::hours(24));
        if (end_of_day > last_time) {
            idle_periods.emplace_back(last_time, end_of_day);
        }

        for (const auto &event : diary.Breaks()) {
            boost::posix_time::time_period break_period{ptime(date, session.GetBeginWindow(event)),
                                                        ptime(date, session.GetEndWindow(event)) + event.duration()};

            auto is_satisfied = false;
            for (const auto &idle_period : idle_periods) {
                if (idle_period.intersection(break_period).length() >= event.duration()) {
                    is_satisfied = true;
                    break;
                }
            }

            if (!is_satisfied) {
                return RouteValidatorBase::ValidationResult(std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                        session.CreateContractualBreakViolationError(route, visits.front())));
            }
        }

        return session.ToValidationResult();
    }
}
