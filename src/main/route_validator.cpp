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
                                    rows::SolverWrapper &solver) const {
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

        // find visits with assignment conflicts
        std::unordered_map<rows::CalendarVisit, std::vector<std::pair<rows::ScheduledVisit, rows::Route> > >
                visit_index;
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
                                        std::vector<std::pair<rows::ScheduledVisit, rows::Route> >{
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

        for (const auto &route : routes) {
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

            Route partial_route{carer};
            const auto visits_size = visits_to_use.size();
            for (auto visit_pos = 0; visit_pos < visits_size; ++visit_pos) {
                Route route_candidate{partial_route};
                route_candidate.visits().push_back(visits_to_use[visit_pos]);

                auto validation_result = Validate(route_candidate, solver);
                if (static_cast<bool>(validation_result.error())) {
                    validation_errors.emplace_back(std::move(validation_result.error()));
                    validation_result = RouteValidatorBase::ValidationResult(std::make_unique<ScheduledVisitError>(
                            ValidationSession::CreateMissingInformationError(
                                    route,
                                    visits_to_use[visit_pos],
                                    "validation error reported already")));
                } else {
                    partial_route = std::move(route_candidate);
                }
            }
        }

        if (!validation_errors.empty()) {
            return validation_errors;
        }

        // logic below reports one validation error at a time and therefore is expensive to run
        std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> latest_arrivals;
        std::unordered_set<rows::Carer> selected_bundles;
        for (const auto &visit_bundle : visit_index) {
            if (visit_bundle.second.size() <= 1) {
                // do not look for assignment errors if it is known that at least one route is invalid
                continue;
            }

            for (const auto &visit_route_pair: visit_bundle.second) {
                const auto &route = visit_route_pair.second;
                if (selected_bundles.insert(route.carer()).second) {
                    auto validation_result = Validate(route, solver, latest_arrivals);
                    if (validation_result.error()) {
                        validation_errors.emplace_back(std::move(validation_result.error()));
                        return validation_errors;
                    }

                    for (const auto &record: validation_result.schedule().records()) {
                        const auto &calendar_visit = record.Visit.calendar_visit().get();
                        auto find_it = latest_arrivals.find(calendar_visit);
                        if (find_it != std::end(latest_arrivals)) {
                            find_it->second = std::max(find_it->second, record.ArrivalInterval.begin().time_of_day());
                        } else {
                            latest_arrivals.emplace(calendar_visit, record.ArrivalInterval.begin().time_of_day());
                        }
                    }
                }
            }
        }

        bool latest_arrivals_updated = true;
        std::unordered_set<rows::Carer> processed_bundles;
        while (latest_arrivals_updated) {
            latest_arrivals_updated = false;
            processed_bundles.clear();

            for (const auto &visit_bundle : visit_index) {
                if (visit_bundle.second.size() <= 1) {
                    continue;
                }

                for (const auto &visit_route_pair : visit_bundle.second) {
                    const auto &route = visit_route_pair.second;
                    if (selected_bundles.find(route.carer()) == std::end(selected_bundles)) {
                        continue;
                    }

                    if (processed_bundles.insert(route.carer()).second) {
                        auto validation_result = Validate(route, solver, latest_arrivals);
                        if (validation_result.error()) {
                            validation_errors.emplace_back(std::move(validation_result.error()));
                            return validation_errors;
                        }

                        for (const auto &record : validation_result.schedule().records()) {
                            auto find_it = latest_arrivals.find(record.Visit.calendar_visit().get());
                            DCHECK(find_it != std::end(latest_arrivals));

                            LOG(INFO) << record.ArrivalInterval.begin().time_of_day();
                            if (find_it->second < record.ArrivalInterval.begin().time_of_day()) {
                                latest_arrivals_updated = true;
                                find_it->second = record.ArrivalInterval.begin().time_of_day();
                            }
                        }
                    }
                }
            }
        }

        const boost::posix_time::time_period time_horizon(solver.StartHorizon(), solver.EndHorizon());

        std::unordered_set<rows::Carer> processed_visits;
        for (const auto &visit_bundle : visit_index) {
            if (visit_bundle.second.size() <= 1) {
                continue;
            }

            for (const auto &visit_pair : visit_bundle.second) {
                const auto &carer = visit_pair.first.carer().get();
                if (processed_visits.insert(carer).second) {
                    LOG(INFO) << "Visit: " << visit_pair.first.carer().get();
                    const auto diary = solver.problem().diary(carer, visit_pair.first.datetime().date()).get();
                    for (const auto &break_interval : diary.Breaks(time_horizon)) {
                        LOG(INFO) << boost::format("break [%1%,%2%] %3%")
                                     % break_interval.begin().time_of_day()
                                     % break_interval.end().time_of_day()
                                     % break_interval.duration();
                    }

                    const auto validation_result = Validate(visit_pair.second, solver, latest_arrivals);
                    DCHECK(!validation_result.error());
                    for (const auto &record : validation_result.schedule().records()) {
                        LOG(INFO) << boost::format("visit %1% %2% %3% %4%")
                                     % record.ArrivalInterval.begin().time_of_day()
                                     % record.Visit.duration()
                                     % record.Visit.service_user().get().id()
                                     % ((record.Visit.calendar_visit().get().carer_count() > 1) ? 'M' : 'S');
                    }
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

    RouteValidatorBase::ValidationResult RouteValidatorBase::Validate(const rows::Route &route, rows::SolverWrapper &solver) const {
        static const std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> empty_map{};
        return Validate(route, solver, empty_map);
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

    static RouteValidatorBase::ScheduledVisitError CreateSkillNotSatisfied(const rows::Route &route,
                                                                           const rows::ScheduledVisit &visit,
                                                                           const int skill_number) {
        return {RouteValidatorBase::ErrorCode::SKILL_VIOLATION,
                (boost::format("NodeToCarer %1% does not have skill %2% to perform visit %3%.")
                 % route.carer().sap_number()
                 % skill_number
                 % visit.calendar_visit().get().id()).str(),
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

    RouteValidatorBase::ValidationResult SimpleRouteValidatorWithTimeWindows::Validate(const rows::Route &route,
                                                                                       rows::SolverWrapper &solver,
                                                                                       const std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> &latest_arrival_times) const {
        using boost::posix_time::time_duration;
        using boost::posix_time::seconds;

        ValidationSession session{route, solver};
        session.Initialize(latest_arrival_times);

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
              error_{nullptr},
              activities_{} {}

    RouteValidatorBase::ValidationResult::ValidationResult(RouteValidatorBase::Metrics metrics,
                                                           Schedule schedule,
                                                           std::list<std::shared_ptr<FixedDurationActivity> > activities)
            : metrics_{std::move(metrics)},
              schedule_{std::move(schedule)},
              error_{nullptr},
              activities_{std::move(activities)} {}

    RouteValidatorBase::ValidationResult::ValidationResult(
            std::unique_ptr<RouteValidatorBase::ValidationError> &&error) noexcept
            : metrics_{},
              schedule_{},
              error_{std::move(error)},
              activities_{} {}

    RouteValidatorBase::ValidationResult::ValidationResult(RouteValidatorBase::ValidationResult &&other) noexcept
            : metrics_{std::move(other.metrics_)},
              schedule_{std::move(other.schedule_)},
              error_{std::move(other.error_)},
              activities_{std::move(other.activities_)} {}

    std::unique_ptr<RouteValidatorBase::ValidationError> &RouteValidatorBase::ValidationResult::error() {
        return error_;
    }

    const std::unique_ptr<RouteValidatorBase::ValidationError> &RouteValidatorBase::ValidationResult::error() const {
        return error_;
    }

    RouteValidatorBase::ValidationResult &RouteValidatorBase::ValidationResult::operator=(
            RouteValidatorBase::ValidationResult &&other) noexcept {
        metrics_ = std::move(other.metrics_);
        schedule_ = std::move(other.schedule_);
        error_ = std::move(other.error_);
        return *this;
    }

    const RouteValidatorBase::Metrics &RouteValidatorBase::ValidationResult::metrics() const {
        return metrics_;
    }

    const Schedule &RouteValidatorBase::ValidationResult::schedule() const {
        return schedule_;
    }

    const std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> > &
    RouteValidatorBase::ValidationResult::activities() const {
        return activities_;
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
            return boost::posix_time::seconds(solver_.GetBeginVisitWindow(interval.begin().time_of_day()));
        }

        return interval.begin().time_of_day();
    }

    boost::posix_time::time_duration ValidationSession::GetEndWindow(
            const Event &interval) const {
        if (!breaks_.empty() && breaks_.front() != interval && breaks_.back() != interval) {
            return boost::posix_time::seconds(solver_.GetEndVisitWindow(interval.begin().time_of_day()));
        }

        return interval.begin().time_of_day();
    }

    boost::posix_time::time_duration ValidationSession::GetBeginWindow(const ScheduledVisit &visit) const {
        const auto earliest_arrival = static_cast<boost::posix_time::time_duration>(
                boost::posix_time::seconds(solver_.GetBeginVisitWindow(visit.datetime().time_of_day())));
        const auto find_it = latest_arrival_times_.find(visit.calendar_visit().get());
        if (find_it != std::end(latest_arrival_times_)) {
            return std::max(earliest_arrival, find_it->second);
        }
        return earliest_arrival;
    }

    boost::posix_time::time_duration ValidationSession::GetEndWindow(const ScheduledVisit &visit) const {
        return boost::posix_time::seconds(solver_.GetEndVisitWindow(visit.datetime().time_of_day()));
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
              current_time_(),
              date_(boost::gregorian::not_a_date_time),
              latest_arrival_times_() {}

    void ValidationSession::Initialize(
            const std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> &latest_arrival_times) {
        using boost::posix_time::seconds;
        using boost::posix_time::time_duration;

        latest_arrival_times_ = latest_arrival_times;

        visits_ = route_.visits();
        if (visits_.empty()) {
            return;
        }

        date_ = visits_.front().datetime().date();
        const auto route_size = visits_.size();
        for (auto visit_pos = 1u; visit_pos < route_size; ++visit_pos) {
            if (visits_[visit_pos].datetime().date() != date_) {
                error_ = std::make_unique<RouteValidatorBase::ValidationError>(
                        CreateValidationError("Route contains visits that span across multiple days"));
                return;
            }
        }

        const auto diary = solver_.problem().diary(route_.carer(), date_);
        if (!diary.is_initialized()) {
            error_ = std::make_unique<RouteValidatorBase::ValidationError>(
                    CreateValidationError((boost::format("Carer %1% is absent on %2%")
                                           % route_.carer()
                                           % date_).str()));
            return;
        }

        auto work_interval_it = std::begin(diary.get().events());
        const auto work_interval_end_it = std::end(diary.get().events());
        if (work_interval_it == work_interval_end_it) {
            error_ = std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                    CreateContractualBreakViolationError(route_, visits_.back()));
            return;
        }

        nodes_.push_back(RealProblemData::DEPOT);
        for (const auto &visit : visits_) {
            nodes_.push_back(GetNode(visit));
        }
        nodes_.push_back(RealProblemData::DEPOT);

        last_node_ = nodes_.front();
        current_visit_ = 0;
        current_node_ = nodes_[1];
        next_node_ = RealProblemData::DEPOT;
        if (nodes_.size() > 2) {
            next_node_ = nodes_[2];
        }

        const boost::posix_time::time_period time_horizon(solver_.StartHorizon(), solver_.EndHorizon());
        breaks_ = diary.get().Breaks(time_horizon);
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

        auto last_node = RealProblemData::DEPOT;
        for (auto node_pos = 1; node_pos < nodes_.size(); ++node_pos) {
            auto current_node = nodes_[node_pos];
            total_travel_time_ += boost::posix_time::seconds(solver_.Distance(last_node, current_node));
            last_node = current_node;
        }

        for (const auto &visit : visits_) {
            total_service_time_ += visit.duration();
        }
    }

    void ValidationSession::Perform(const ScheduledVisit &visit) {
        using boost::posix_time::time_duration;

        const time_duration earliest_service_start{GetBeginWindow(visit)};
        const time_duration latest_service_start{GetEndWindow(visit)};

        const auto travel_time = GetTravelTime(last_node_, current_node_);
        const auto arrival_time = current_time_ + travel_time;
        const auto &service_start = std::max(arrival_time, earliest_service_start);
        if (util::COMP_GT(service_start, latest_service_start)) {
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

        schedule_.Add(boost::posix_time::ptime(date_, service_start), travel_time, visit);

        last_node_ = current_node_;
        current_node_ = next_node_;

        ++current_visit_;
        if (current_visit_ + 1 < visits_.size()) {
            next_node_ = GetNode(visits_[current_visit_ + 1]);
        } else {
            next_node_ = RealProblemData::DEPOT;
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
        if (util::COMP_GT(break_start, latest_break_start)) {
            std::stringstream stream_msg;
            stream_msg << "[BREAK_CONSTRAINT_VIOLATION]"
                       << " [ " << earliest_break_start << "," << latest_break_start << " ]"
                       << " break_start: " << break_start;

            VLOG(2) << stream_msg.rdbuf();

            ScheduledVisit visit_to_use;
            if (current_visit_ > 1) {
                visit_to_use = visits_[current_visit_ - 1];
            } else {
                visit_to_use = visits_.front();
            }

            error_ = std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                    CreateContractualBreakViolationError(route_, visit_to_use));
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

    bool ValidationSession::StartsAfter(const boost::posix_time::time_duration &time_of_day,
                                        const ScheduledVisit &visit) const {
        return util::COMP_GE(GetBeginWindow(visit), time_of_day + GetTravelTime(last_node_, current_node_));
    }

    bool ValidationSession::CanPerformAfter(const boost::posix_time::time_duration &time_of_day,
                                            const Event &break_interval) const {
        return util::COMP_GE(GetEndWindow(break_interval), time_of_day);
    }

    bool ValidationSession::CanPerformAfter(const boost::posix_time::time_duration &time_of_day,
                                            const ScheduledVisit &visit) const {
        return util::COMP_GE(GetEndWindow(visit), time_of_day + GetTravelTime(last_node_, current_node_));
    }

    RouteValidatorBase::ValidationResult ValidationSession::ToValidationResult(
            std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> > activities) {
        if (error_) {
            return RouteValidatorBase::ValidationResult(std::move(error_));
        }

        return RouteValidatorBase::ValidationResult{
                RouteValidatorBase::Metrics{total_available_time_,
                                            total_service_time_,
                                            total_travel_time_},
                schedule_,
                std::move(activities)};
    }

    RouteValidatorBase::ValidationResult ValidationSession::ToValidationResult() {
        return ToValidationResult(std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> >{});
    }

    boost::posix_time::time_duration ValidationSession::GetTravelTime(
            operations_research::RoutingNodeIndex from_node,
            operations_research::RoutingNodeIndex to_node) const {
        return boost::posix_time::seconds(solver_.Distance(from_node, to_node));
    }

    operations_research::RoutingNodeIndex ValidationSession::GetNode(const ScheduledVisit &visit) const {
        return *std::begin(solver_.GetNodes(visit));
    }

    bool ValidationSession::error() const {
        return bool(error_);
    }

    boost::posix_time::time_duration ValidationSession::current_time() const {
        return current_time_;
    }

    // does not perform as extensive tests as validate full hence may return false positives if that is the case
    // then should call validate full for the final confirmation
    RouteValidatorBase::ValidationResult SolutionValidator::ValidateFast(int vehicle,
                                                                         const operations_research::Assignment &solution,
                                                                         const operations_research::RoutingIndexManager &index_manager,
                                                                         const operations_research::RoutingModel &model,
                                                                         SolverWrapper &solver) const {
        static const std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> NO_OVERRIDE_ARRIVAL;

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
                                solver.NodeToVisit(index_manager.IndexToNode(node_index)));
        }
        CHECK_EQ(visits.size() + 2, indices.size());

        Route route{carer, visits};
        ValidationSession session{route, solver};
        session.Initialize(NO_OVERRIDE_ARRIVAL);

        if (session.error() || visits.empty()) {
            return session.ToValidationResult();
        }

        const auto date = visits.front().datetime().date();
        const auto diary = solver.problem().diary(carer, date).get();

        // build intervals where carer may not work
        std::vector<boost::posix_time::time_period> idle_periods;

        const auto &time_dim = model.GetDimensionOrDie(SolverWrapper::TIME_DIMENSION);
        auto last_node = RealProblemData::DEPOT;
        const ptime start_of_day{date, boost::posix_time::seconds(0)};
        const ptime end_of_day = solver.EndHorizon();
        for (auto node_pos = 1; node_pos < indices.size() - 1; ++node_pos) {
            const auto visit_index = indices[node_pos];
            const auto &visit = visits[node_pos - 1];
            const ptime fastest_arrival{date, seconds(solver.GetBeginVisitWindow(visit.datetime().time_of_day()))};
            const ptime latest_arrival{date, seconds(solver.GetEndVisitWindow(visit.datetime().time_of_day()))};
            const ptime arrival{date, seconds(solution.Min(time_dim.CumulVar(visit_index)))};
            const time_period arrival_period{fastest_arrival, latest_arrival};
            if (!arrival_period.contains(arrival)) {
                static const boost::posix_time::time_duration MIN_DURATION{0, 0, 1};

                boost::posix_time::time_duration effective_delay;
                if (arrival_period.is_before(arrival)) {
                    effective_delay = arrival - arrival_period.end();
                } else {
                    effective_delay = arrival_period.begin() - arrival;
                }

                if (effective_delay > MIN_DURATION) {
                    LOG(FATAL) << boost::format("Arrival time %1% is expected to be outside the interval %2%")
                                  % arrival
                                  % arrival_period;
                    std::unique_ptr<RouteValidatorBase::ValidationError> error_ptr
                            = std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                                    session.CreateLateArrivalError(route, visit, effective_delay));
                    return RouteValidatorBase::ValidationResult(std::move(error_ptr));
                }
            }
        }

        const auto first_node_pos = 1;
        const auto first_visit_index = indices.at(first_node_pos);
        const auto &first_visit = visits.at(0);
        const ptime first_visit_latest_start{date, seconds(solution.Max(time_dim.CumulVar(first_visit_index)))};
        idle_periods.emplace_back(start_of_day, first_visit_latest_start);

        for (auto current_node_pos = 1; current_node_pos < indices.size() - 2; ++current_node_pos) {
            const auto current_node_index = indices.at(current_node_pos);
            const auto &current_visit = visits.at(current_node_pos);
            const auto next_node_pos = current_node_pos + 1;
            const auto next_node_index = indices.at(next_node_pos);
            const auto travel_time = session.GetTravelTime(index_manager.IndexToNode(current_node_index),
                                                           index_manager.IndexToNode(next_node_index));

            const ptime min_current_start{date, seconds(solution.Min(time_dim.CumulVar(current_node_index)))};
            const ptime min_current_finish = min_current_start + current_visit.duration();
            const ptime max_next_start{date, seconds(solution.Max(time_dim.CumulVar(next_node_index)))};
            if (travel_time > boost::posix_time::seconds(0)) {
                // travel before
                time_period travel_before{min_current_finish + travel_time, max_next_start};
                if (travel_before.length() > boost::posix_time::seconds(0)) {
                    idle_periods.push_back(travel_before);
                }

                // travel after
                time_period travel_after{min_current_finish, max_next_start - travel_time};
                if (travel_after.length() > boost::posix_time::seconds(0)) {
                    idle_periods.push_back(travel_after);
                }
            } else {
                // no travel at all
                time_period no_travel{min_current_finish, max_next_start};
                if (no_travel.length() > boost::posix_time::seconds(0)) {
                    idle_periods.push_back(no_travel);
                }
            }
        }

        const auto last_node_pos = indices.size() - 2;
        const auto last_visit_index = indices.at(last_node_pos);
        const auto &last_visit = visits.back();
        const ptime last_visit_fastest_start{date, boost::posix_time::seconds(
                solution.Min(time_dim.CumulVar(last_visit_index)))};
        const auto last_visit_fastest_finish = last_visit_fastest_start + last_visit.duration();
        if (end_of_day > last_visit_fastest_finish) {
            idle_periods.emplace_back(last_visit_fastest_finish, end_of_day);
        }

        for (const auto &event : solver.GetEffectiveBreaks(diary)) {
            boost::posix_time::time_period break_period{ptime(date, session.GetBeginWindow(event)),
                                                        ptime(date, session.GetEndWindow(event)) + event.duration()};
            boost::posix_time::time_duration duration_after_end_of_day = boost::posix_time::seconds(0);
            if (break_period.end() > end_of_day) {
                duration_after_end_of_day = break_period.end() - end_of_day;
            }

            auto is_satisfied = false;
            for (const auto &idle_period : idle_periods) {
                if (idle_period.intersection(break_period).length() + duration_after_end_of_day >= event.duration()) {
                    is_satisfied = true;
                    break;
                }
            }

            if (!is_satisfied) {
                LOG(ERROR) << "Break constraint violation";
                LOG(ERROR) << boost::format("Did not find an interval window for the break: (%1%, %2%)")
                              % break_period.begin()
                              % break_period.end();
                LOG(INFO) << "End of day: " << last_visit_fastest_finish << " " << duration_after_end_of_day << " "
                          << last_visit.duration();
                LOG(ERROR) << "Available periods:";
                for (const auto &period : idle_periods) {
                    LOG(ERROR) << boost::format("(%1%,%2%)") % period.begin() % period.end();
                }
                LOG(ERROR) << "Verbose information";
                LOG(ERROR) << "Vehicle: " << vehicle << " " << carer;
                LOG(ERROR) << "Scheduled visits: ";
                for (const auto &visit : visits) {
                    LOG(INFO) << visit;
                }

                std::unique_ptr<rows::RouteValidatorBase::ValidationError> error_ptr
                        = std::make_unique<rows::RouteValidatorBase::ScheduledVisitError>(
                                session.CreateContractualBreakViolationError(route, visits.front()));
                return RouteValidatorBase::ValidationResult(std::move(error_ptr));
            }
        }

        return session.ToValidationResult();
    }

    RouteValidatorBase::ValidationResult SolutionValidator::ValidateFull(int vehicle,
                                                                         const operations_research::Assignment &solution,
                                                                         const operations_research::RoutingIndexManager &index_manager,
                                                                         const operations_research::RoutingModel &model,
                                                                         rows::SolverWrapper &solver) const {
        static const std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> NO_OVERRIDE_ARRIVAL;

        using boost::posix_time::seconds;
        using boost::posix_time::ptime;
        using boost::posix_time::time_period;
        using boost::date_time::date;
        using boost::date_time::time_duration;

        const auto &carer = solver.Carer(vehicle);
        auto current_index = model.Start(vehicle);
        std::vector<int64> indices;
        indices.push_back(current_index);
        while (!model.IsEnd(current_index)) {
            current_index = solution.Value(model.NextVar(current_index));
            indices.push_back(current_index);
        }
        CHECK_GE(indices.size(), 2);

        std::vector<ScheduledVisit> visits;
        for (auto node_pos = 1; node_pos < indices.size() - 1; ++node_pos) {
            const auto node_index = indices[node_pos];
            visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN,
                                carer,
                                solver.NodeToVisit(index_manager.IndexToNode(node_index)));
        }

        Route route{carer, visits};
        ValidationSession session{route, solver};
        session.Initialize(NO_OVERRIDE_ARRIVAL);

        if (session.error() || visits.empty()) {
            return session.ToValidationResult();
        }

        std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> > activities;
        const auto &time_dim = model.GetDimensionOrDie(SolverWrapper::TIME_DIMENSION);
        const auto today = visits.front().datetime().date();
        const auto diary = solver.problem().diary(carer, today).get();
        auto last_visit_node = RealProblemData::DEPOT;
        boost::posix_time::ptime last_min_visit_complete = boost::posix_time::not_a_date_time;
        boost::posix_time::ptime last_max_visit_complete = boost::posix_time::not_a_date_time;
        for (std::size_t node_pos = 1; node_pos < indices.size() - 1; ++node_pos) {
            const auto visit_index = indices[node_pos];
            const auto current_visit_node = index_manager.IndexToNode(visit_index);
            const auto &visit = visits[node_pos - 1];

            for (const auto task : visit.calendar_visit()->tasks()) {
                if (std::find(std::cbegin(carer.skills()), std::cend(carer.skills()), task) == std::cend(carer.skills())) {
                    LOG(FATAL) << boost::format("Carer %1% does not have skill %2% to perform the visit %3%")
                                  % carer.sap_number()
                                  % task
                                  % visit.calendar_visit()->id();

                    std::unique_ptr<RouteValidatorBase::ValidationError> error_ptr = std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                            session.CreateSkillNotSatisfied(route, visit, task));
                    return RouteValidatorBase::ValidationResult(std::move(error_ptr));
                }
            }

            const ptime fastest_arrival{today, seconds(solver.GetBeginVisitWindow(visit.calendar_visit()->time_windows().begin().time_of_day()))};
            const ptime latest_arrival{today, seconds(solver.GetEndVisitWindow(visit.calendar_visit()->time_windows().end().time_of_day()))};
            const ptime min_arrival{today, seconds(solution.Min(time_dim.CumulVar(visit_index)))};
            const ptime max_arrival{today, seconds(solution.Max(time_dim.CumulVar(visit_index)))};

            const time_period arrival_period{fastest_arrival, latest_arrival};
            if (!arrival_period.contains(min_arrival)) {
                static const boost::posix_time::time_duration MIN_DURATION{0, 0, 1};

                boost::posix_time::time_duration effective_delay;
                if (arrival_period.is_before(min_arrival)) {
                    effective_delay = min_arrival - arrival_period.end();
                } else {
                    effective_delay = arrival_period.begin() - min_arrival;
                }

                if (effective_delay > MIN_DURATION) {
                    LOG(FATAL) << boost::format("Arrival time %1% is expected to be outside the interval %2%")
                                  % min_arrival
                                  % arrival_period;
                    std::unique_ptr<RouteValidatorBase::ValidationError> error_ptr = std::make_unique<RouteValidatorBase::ScheduledVisitError>(
                            session.CreateLateArrivalError(route, visit, effective_delay));
                    return RouteValidatorBase::ValidationResult(std::move(error_ptr));
                }
            }

            if (last_visit_node != RealProblemData::DEPOT) {
                const auto travel_time = boost::posix_time::seconds(
                        solver.Distance(last_visit_node, current_visit_node));
                const auto max_departure_to_arrive_on_time = max_arrival - travel_time;
//                const auto max_departure = std::min(last_max_visit_complete, max_departure_to_arrive_on_time);
                CHECK_LE(last_min_visit_complete, max_departure_to_arrive_on_time);

                activities.emplace_back(std::make_shared<RouteValidatorBase::FixedDurationActivity>(
                        (boost::format("Travel %1%-%2%") % last_visit_node % current_visit_node).str(),
                        boost::posix_time::time_period{last_min_visit_complete, max_departure_to_arrive_on_time},
                        travel_time,
                        RouteValidatorBase::ActivityType::Travel));
            }

            activities.emplace_back(std::make_shared<RouteValidatorBase::FixedDurationActivity>(
                    (boost::format("Visit %1%") % current_visit_node).str(),
                    boost::posix_time::time_period(min_arrival, (max_arrival - min_arrival)),
                    visit.duration(),
                    RouteValidatorBase::ActivityType::Visit));

            last_visit_node = current_visit_node;
            last_min_visit_complete = min_arrival + visit.duration();
            last_max_visit_complete = max_arrival + visit.duration();
        }

        const auto effective_breaks = solver.GetEffectiveBreaks(diary);
        boost::posix_time::time_duration start_time;
        std::vector<std::shared_ptr<RouteValidatorBase::FixedDurationActivity>> breaks_to_distribute;
        if (solver.out_office_hours_breaks_enabled()) {
            activities.emplace_front(std::make_shared<RouteValidatorBase::FixedDurationActivity>("before working hours",
                                                                                                 effective_breaks.front().period(),
                                                                                                 effective_breaks.front().duration(),
                                                                                                 RouteValidatorBase::ActivityType::Break));
            activities.emplace_back(std::make_shared<RouteValidatorBase::FixedDurationActivity>("after working hours",
                                                                                                effective_breaks.back().period(),
                                                                                                effective_breaks.back().duration(),
                                                                                                RouteValidatorBase::ActivityType::Break));
            auto break_index = 1;
            const auto end_it = effective_breaks.end() - 1;
            for (auto break_it = effective_breaks.begin() + 1; break_it != end_it; ++break_it) {
                const ptime begin_window{today, boost::posix_time::seconds(
                        solver.GetBeginBreakWindow(break_it->begin().time_of_day()))};
                const ptime end_window{today, boost::posix_time::seconds(
                        solver.GetEndBreakWindow(break_it->begin().time_of_day()))};
                breaks_to_distribute.emplace_back(std::make_shared<RouteValidatorBase::FixedDurationActivity>(
                        "break " + std::to_string(break_index++),
                        boost::posix_time::time_period{begin_window, end_window},
                        break_it->duration(),
                        RouteValidatorBase::ActivityType::Break));
            }
        } else {
            start_time = boost::posix_time::seconds(solution.Min(time_dim.CumulVar(indices[1])));
            auto break_index = 1;
            for (const auto &break_event : effective_breaks) {
                const ptime begin_window{today, boost::posix_time::seconds(
                        solver.GetBeginBreakWindow(break_event.begin().time_of_day()))};
                const ptime end_window{today, boost::posix_time::seconds(
                        solver.GetEndBreakWindow(break_event.begin().time_of_day()))};
                breaks_to_distribute.emplace_back(std::make_shared<RouteValidatorBase::FixedDurationActivity>(
                        "break " + std::to_string(break_index++),
                        boost::posix_time::time_period{begin_window, end_window},
                        break_event.duration(),
                        RouteValidatorBase::ActivityType::Break));
            }
        }

        const auto start_date_time = ptime(today, start_time);
        auto failed_activity_ptr = try_get_failed_activity(activities, start_date_time);
        if (failed_activity_ptr) {
            std::string error_msg = "Failed to perform " + failed_activity_ptr->debug_info();
            LOG(FATAL) << error_msg;
            std::unique_ptr<RouteValidatorBase::ValidationError> error_ptr
                    = std::make_unique<RouteValidatorBase::ValidationError>(RouteValidatorBase::ErrorCode::UNKNOWN,
                                                                            std::move(error_msg));
            return RouteValidatorBase::ValidationResult(std::move(error_ptr));
        }

        if (breaks_to_distribute.empty()) {
            return session.ToValidationResult();
        }

        if (is_schedule_valid(activities, breaks_to_distribute, ptime(today, start_time), std::begin(activities),
                              std::begin(breaks_to_distribute))) {
            return session.ToValidationResult(activities);
        }

        LOG(INFO) << "Vehicle: " << vehicle;

        for (const auto &activity : activities) {
            LOG(INFO) << activity->debug_info()
                      << ' ' << boost::posix_time::to_simple_string(activity->period())
                      << ' ' << activity->duration();
        }

        for (const auto &activity : breaks_to_distribute) {
            LOG(INFO) << activity->debug_info()
                      << ' ' << boost::posix_time::to_simple_string(activity->period())
                      << ' ' << activity->duration();
        }

        std::string error_msg = "Failed to find a combination of breaks that would create a valid activity sequence";
        LOG(FATAL) << error_msg;
        std::unique_ptr<RouteValidatorBase::ValidationError> error_ptr
                = std::make_unique<RouteValidatorBase::ValidationError>(RouteValidatorBase::ErrorCode::UNKNOWN,
                                                                        std::move(error_msg));
        return RouteValidatorBase::ValidationResult(std::move(error_ptr));
    }

    std::shared_ptr<RouteValidatorBase::FixedDurationActivity> SolutionValidator::try_get_failed_activity(
            std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> > &activities,
            const boost::posix_time::ptime &start_date_time) const {
        auto current_time = start_date_time;
        for (const auto &activity : activities) {
            current_time = activity->Perform(current_time);
            if (current_time.is_not_a_date_time()) {
                return activity;
            }
        }
        return nullptr;
    }

    bool SolutionValidator::is_schedule_valid(
            std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> > &activities,
            const std::vector<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> > &normal_breaks,
            const boost::posix_time::ptime start_date_time,
            std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> >::iterator current_position,
            std::vector<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> >::iterator current_break) const {
        if (current_break == std::end(normal_breaks)) {
            // no more breaks to distribute
            return try_get_failed_activity(activities, start_date_time) == nullptr;
        }

        auto activity_it = current_position;
        for (; activity_it != std::end(activities) && (*current_break)->IsAfter(**activity_it); ++activity_it);
        for (; activity_it != std::end(activities); ++activity_it) {
            auto inserted_element = activities.insert(activity_it, *current_break);
            if (is_schedule_valid(activities, normal_breaks, start_date_time, activity_it, std::next(current_break))) {
                return true;
            }
            activities.erase(inserted_element);

            if ((*current_break)->IsBefore(**activity_it)) {
                return false;
            }
        }

        auto current_break_pos = current_break;
        while (current_break_pos != std::end(normal_breaks)) {
            activities.emplace_back(*current_break_pos);
            ++current_break_pos;
        }

        if (try_get_failed_activity(activities, start_date_time) == nullptr) {
            return true;
        }

        current_break_pos = current_break;
        while (current_break_pos != std::end(normal_breaks)) {
            activities.pop_back();
            ++current_break_pos;
        }

        return false;
    }

    RouteValidatorBase::ValidationResult SolutionValidator::ValidateFull(const operations_research::Assignment &solution,
                                                                         const operations_research::RoutingIndexManager &index_manager,
                                                                         const operations_research::RoutingModel &model,
                                                                         rows::SolverWrapper &solver) const {
        std::unordered_map<ServiceUser, std::unordered_set<int> > user_visited_vehicles;
        std::unordered_map<ServiceUser, bool> has_multiple_carer_visits;

        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            auto validation_result = ValidateFull(vehicle, solution, index_manager, model, solver);
            if (validation_result.error()) {
                return std::move(validation_result);
            }

            auto current_index = model.Start(vehicle);
            std::vector<int64> visit_indices;
            visit_indices.push_back(current_index);
            while (!model.IsEnd(current_index)) {
                current_index = solution.Value(model.NextVar(current_index));
                visit_indices.push_back(current_index);
            }
            CHECK_GE(visit_indices.size(), 2);

            for (std::size_t position = 1; position < visit_indices.size() - 1; ++position) {
                const auto &visit = solver.NodeToVisit(index_manager.IndexToNode(visit_indices.at(position)));

                auto find_it = user_visited_vehicles.find(visit.service_user());
                if (find_it == std::end(user_visited_vehicles)) {
                    user_visited_vehicles.emplace(visit.service_user(), std::unordered_set<int>{vehicle});
                    has_multiple_carer_visits.emplace(visit.service_user(), true);
                } else {
                    find_it->second.insert(vehicle);
                    if (visit.carer_count() > 1) {
                        has_multiple_carer_visits.at(visit.service_user()) = true;
                    }
                }
            }
        }

        for (const auto &user_vehicles_pair : user_visited_vehicles) {
            auto max_visiting_vehicles = SolverWrapper::MAX_CARERS_SINGLE_VISITS;
            if (has_multiple_carer_visits.at(user_vehicles_pair.first)) {
                max_visiting_vehicles = SolverWrapper::MAX_CARERS_MULTIPLE_VISITS;
            }

            if (user_vehicles_pair.second.size() > max_visiting_vehicles) {
                std::string error_msg = (boost::format("ServiceUser %1% is visited by %2% carers which is above the limit of %3%")
                                         % user_vehicles_pair.first
                                         % user_vehicles_pair.second.size()
                                         % max_visiting_vehicles).str();
                LOG(FATAL) << error_msg;

                return RouteValidatorBase::ValidationResult(std::make_unique<RouteValidatorBase::ValidationError>(
                        RouteValidatorBase::ValidationError{RouteValidatorBase::ErrorCode::TOO_MANY_CARERS, std::move(error_msg)}));
            }
        }

        return RouteValidatorBase::ValidationResult();
    }

    Schedule::Record::Record()
            : ArrivalInterval(boost::posix_time::ptime(), boost::posix_time::seconds(0)),
              TravelTime(boost::posix_time::not_a_date_time),
              Visit{} {}

    Schedule::Record::Record(boost::posix_time::time_period arrival_interval,
                             boost::posix_time::time_duration travel_time,
                             ScheduledVisit visit)
            : ArrivalInterval(arrival_interval),
              TravelTime(std::move(travel_time)),
              Visit(std::move(visit)) {}

    Schedule::Record::Record(const Schedule::Record &other)
            : ArrivalInterval(other.ArrivalInterval),
              TravelTime(other.TravelTime),
              Visit(other.Visit) {}

    Schedule::Record &Schedule::Record::operator=(const Schedule::Record &other) {
        ArrivalInterval = other.ArrivalInterval;
        TravelTime = other.TravelTime;
        Visit = other.Visit;
        return *this;
    }

    void Schedule::Add(boost::posix_time::ptime arrival,
                       boost::posix_time::time_duration travel_time,
                       const ScheduledVisit &visit) {
        records_.emplace_back(boost::posix_time::time_period(arrival, arrival), std::move(travel_time), visit);
    }

    boost::optional<Schedule::Record> Schedule::Find(const ScheduledVisit &visit) const {
        for (const auto &record : records_) {
            if (record.Visit == visit) {
                return boost::make_optional(record);
            }
        }
        return boost::none;
    }

    Schedule::Schedule()
            : records_() {}

    Schedule::Schedule(
            const Schedule &other)
            : records_(other.records_) {}

    Schedule &Schedule::operator=(const Schedule &other) {
        records_ = other.records_;
        return *this;
    }

    const std::vector<Schedule::Record> &Schedule::records() const {
        return records_;
    }

    RouteValidatorBase::FixedDurationActivity::FixedDurationActivity(std::string debug_info,
                                                                     boost::posix_time::time_period start_window,
                                                                     boost::posix_time::time_duration duration,
                                                                     ActivityType activity_type)
            : debug_info_{std::move(debug_info)},
              interval_{start_window.begin(), start_window.end() + duration},
              start_window_{start_window},
              duration_{std::move(duration)},
              activity_type_{activity_type} {}

    boost::posix_time::ptime RouteValidatorBase::FixedDurationActivity::Perform(
            boost::posix_time::ptime current_time) const {
        if (duration_.total_seconds() == 0) {
            return current_time;
        }

        if (start_window_.is_before(current_time) && start_window_.end() != current_time) {
            return boost::posix_time::not_a_date_time;
        } else if (start_window_.contains(current_time)
                   || start_window_.begin() == current_time
                   || start_window_.end() == current_time) {
            return current_time + duration_;
        } else {
            if (start_window_.is_after(current_time)
                || (start_window_.begin() == start_window_.end() && start_window_.begin() >= current_time)) {
                return start_window_.begin() + duration_;
            }
            return boost::posix_time::not_a_date_time;
        }
    }

    std::string RouteValidatorBase::FixedDurationActivity::debug_info() const {
        return (boost::format("%1% - [%2%..%3%] for %4%")
                % debug_info_
                % start_window_.begin().time_of_day()
                % start_window_.end().time_of_day()
                % duration_).str();
    }

    RouteValidatorBase::ActivityType RouteValidatorBase::FixedDurationActivity::activity_type() const {
        return activity_type_;
    }

    boost::posix_time::time_duration RouteValidatorBase::FixedDurationActivity::duration() const {
        return duration_;
    }

    bool RouteValidatorBase::FixedDurationActivity::IsBefore(const FixedDurationActivity &other) const {
        return interval_.is_before(other.interval_.begin());
    }

    bool RouteValidatorBase::FixedDurationActivity::IsAfter(const FixedDurationActivity &other) const {
        return interval_.is_after(other.interval_.end());
    }

    boost::posix_time::time_period RouteValidatorBase::FixedDurationActivity::period() const {
        return start_window_;
    }

    std::ostream &operator<<(std::ostream &out, RouteValidatorBase::ActivityType activity_type) {
        switch (activity_type) {
            case rows::RouteValidatorBase::ActivityType::Visit:
                out << "visit";
                break;
            case rows::RouteValidatorBase::ActivityType::Break:
                out << "break";
                break;
            case rows::RouteValidatorBase::ActivityType::Travel:
                out << "travel";
                break;
            default:
                LOG(FATAL) << "ActivityType " << static_cast<int>(activity_type)
                           << " does not have a human readable name";
        }
        return out;
    }
}
