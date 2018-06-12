#include <algorithm>
#include <numeric>
#include <vector>
#include <functional>
#include <chrono>
#include <tuple>
#include <cmath>

#include <glog/logging.h>

#include <boost/date_time.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/format.hpp>

#include <osrm/coordinate.hpp>
#include <osrm/util/coordinate.hpp>

#include <ortools/constraint_solver/routing_flags.h>
#include <ortools/sat/integer_expr.h>
#include <util/aplication_error.h>

#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <osrm/engine/api/route_parameters.hpp>


#include "calendar_visit.h"
#include "carer.h"
#include "location.h"
#include "scheduled_visit.h"
#include "solution.h"
#include "solver_wrapper.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"

// TODO: add support for mobile workers
// TODO: add information about partner
// TODO: add information about back to back carers
// TODO: modify cost function to differentiate between each of employee category

std::vector<rows::Location> DistinctLocations(const rows::Problem &problem) {
    std::unordered_set<rows::Location> locations;
    for (const auto &visit : problem.visits()) {
        const auto &location_opt = visit.location();
        if (location_opt) {
            locations.insert(location_opt.get());
        }
    }

    return {std::cbegin(locations), std::cend(locations)};
}

namespace rows {

    const operations_research::RoutingModel::NodeIndex SolverWrapper::DEPOT{0};

    const int64 SolverWrapper::SECONDS_IN_DAY = 24 * 3600;

    const std::string SolverWrapper::TIME_DIMENSION{"Time"};

    SolverWrapper::SolverWrapper(const rows::Problem &problem,
                                 osrm::EngineConfig &config,
                                 const operations_research::RoutingSearchParameters &search_parameters)
            : SolverWrapper(problem,
                            config,
                            search_parameters,
                            boost::posix_time::minutes(120),
                            boost::posix_time::minutes(120),
                            boost::posix_time::not_a_date_time) {}

    SolverWrapper::SolverWrapper(const rows::Problem &problem, osrm::EngineConfig &config,
                                 const operations_research::RoutingSearchParameters &search_parameters,
                                 boost::posix_time::time_duration visit_time_window,
                                 boost::posix_time::time_duration break_time_window,
                                 boost::posix_time::time_duration begin_end_work_day_adjustment)
            : SolverWrapper(problem,
                            DistinctLocations(problem),
                            config,
                            search_parameters,
                            visit_time_window,
                            break_time_window,
                            begin_end_work_day_adjustment) {}

    SolverWrapper::SolverWrapper(const rows::Problem &problem,
                                 const std::vector<rows::Location> &locations,
                                 osrm::EngineConfig &config,
                                 const operations_research::RoutingSearchParameters &search_parameters,
                                 boost::posix_time::time_duration visit_time_window,
                                 boost::posix_time::time_duration break_time_window,
                                 boost::posix_time::time_duration begin_end_work_day_adjustment)
            : problem_(problem),
              depot_(Location::GetCentralLocation(std::cbegin(locations), std::cend(locations))),
              depot_service_user_(),
              visit_time_window_(visit_time_window),
              break_time_window_(break_time_window),
              begin_end_work_day_adjustment_(begin_end_work_day_adjustment),
            // time when carer is out of office is considered as a break
              out_office_hours_breaks_enabled_(true),
              location_container_(std::cbegin(locations), std::cend(locations), config),
              parameters_(search_parameters),
              visit_index_(),
              visit_by_node_(),
              service_users_() {

        visit_by_node_.emplace_back(CalendarVisit()); // depot visit
        // visit that needs multiple carers is referenced by multiple nodes
        // all such nodes must be either performed or unperformed
        operations_research::RoutingModel::NodeIndex current_visit_node{1};
        for (const auto &visit : problem_.visits()) {
            DCHECK_GT(visit.carer_count(), 0);

            auto insert_pair = visit_index_.emplace(visit,
                                                    std::unordered_set<operations_research::RoutingModel::NodeIndex>{});
            if (!insert_pair.second) {
                // skip duplicate
                continue;
            }

            auto &node_index_set = insert_pair.first->second;
            const auto visit_start = visit.datetime().time_of_day();
            for (auto carer_count = 0; carer_count < visit.carer_count(); ++carer_count, ++current_visit_node) {
                visit_by_node_.push_back(visit);
                const auto index_inserted = node_index_set.insert(current_visit_node).second;
                DCHECK(index_inserted);
            }
        }
        DCHECK_EQ(current_visit_node.value(), visit_by_node_.size());

        for (const auto &service_user : problem_.service_users()) {
            const auto visit_count = std::count_if(std::cbegin(problem_.visits()),
                                                   std::cend(problem_.visits()),
                                                   [&service_user](const rows::CalendarVisit &visit) -> bool {
                                                       return visit.service_user() == service_user;
                                                   });
            if (visit_count > 0) {
                const auto insert_it = service_users_.insert(
                        std::make_pair(service_user, LocalServiceUser(service_user, visit_count)));
                DCHECK(insert_it.second);
            }
        }
    }

    int64 SolverWrapper::Distance(operations_research::RoutingModel::NodeIndex from,
                                  operations_research::RoutingModel::NodeIndex to) {
        if (from == DEPOT || to == DEPOT) {
            return 0;
        }

        return location_container_.Distance(NodeToVisit(from).location().get(),
                                            NodeToVisit(to).location().get());
    }

    int64 SolverWrapper::ServiceTime(operations_research::RoutingModel::NodeIndex node) {
        if (node == DEPOT) {
            return 0;
        }

        const auto visit = NodeToVisit(node);
        return visit.duration().total_seconds();
    }

    int64 SolverWrapper::ServicePlusTravelTime(operations_research::RoutingModel::NodeIndex from,
                                               operations_research::RoutingModel::NodeIndex to) {
        if (from == DEPOT) {
            return 0;
        }

        const auto service_time = ServiceTime(from);
        const auto travel_time = Distance(from, to);
        return service_time + travel_time;
    }

    boost::optional<rows::Diary>
    FindDiaryOrNone(const std::vector<rows::Diary> &diaries, boost::gregorian::date date) {
        const auto find_date_it = std::find_if(std::cbegin(diaries),
                                               std::cend(diaries),
                                               [&date](const rows::Diary &diary) -> bool {
                                                   return diary.date() == date;
                                               });
        if (find_date_it != std::cend(diaries)) {
            return boost::make_optional(*find_date_it);
        }
        return boost::none;
    }

    const rows::Carer &SolverWrapper::Carer(int vehicle) const {
        return problem_.carers().at(static_cast<std::size_t>(vehicle)).first;
    }

    const rows::SolverWrapper::LocalServiceUser &SolverWrapper::User(const rows::ServiceUser &user) const {
        const auto find_it = service_users_.find(user);
        DCHECK(find_it != std::end(service_users_));
        return find_it->second;
    }

    std::vector<rows::Event> SolverWrapper::GetEffectiveBreaks(const rows::Diary &diary) const {
        const auto original_breaks = diary.Breaks();

        if (original_breaks.size() < 2) {
            return original_breaks;
        }

        std::vector<Event> breaks_to_use;
        if (out_office_hours_breaks_enabled_) {
            const auto &before_work_interval = original_breaks.front();
            const auto start_time_adjustment = boost::posix_time::ptime(
                    before_work_interval.end().date(),
                    boost::posix_time::seconds(GetAdjustedWorkdayStart(before_work_interval.end().time_of_day())));
            breaks_to_use.emplace_back(
                    boost::posix_time::time_period(before_work_interval.begin(), start_time_adjustment)
            );
        }

        std::copy(std::begin(original_breaks) + 1, std::end(original_breaks) - 1, std::back_inserter(breaks_to_use));

        if (out_office_hours_breaks_enabled_) {
            const auto &after_work_interval = original_breaks.back();
            const auto end_time_adjustment = boost::posix_time::ptime(
                    after_work_interval.begin().date(),
                    boost::posix_time::seconds(GetAdjustedWorkdayFinish(after_work_interval.begin().time_of_day())));
            breaks_to_use.emplace_back(
                    boost::posix_time::time_period(end_time_adjustment, after_work_interval.end()));
        }

        return breaks_to_use;
    }

    operations_research::IntervalVar *SolverWrapper::CreateBreakInterval(operations_research::Solver *solver,
                                                                         const rows::Event &event,
                                                                         std::string label) const {
        const auto start_time = event.begin().time_of_day();
        const auto raw_duration = event.duration().total_seconds();
        const auto begin_break_window = this->GetBeginBreakWindow(start_time);
        const auto end_break_window = this->GetEndBreakWindow(start_time);
        return solver->MakeIntervalVar(
                begin_break_window, end_break_window,
                raw_duration, raw_duration,
                begin_break_window + raw_duration,
                end_break_window + raw_duration,
                /*optional*/false,
                label);
    }

    std::vector<operations_research::IntervalVar *> SolverWrapper::CreateBreakIntervals(
            operations_research::Solver *const solver,
            const rows::Carer &carer,
            const rows::Diary &diary) const {
        std::vector<operations_research::IntervalVar *> break_intervals;

        const auto break_periods = GetEffectiveBreaks(diary);
        if (break_periods.empty()) {
            return break_intervals;
        }

        if (out_office_hours_breaks_enabled_) {
            const auto &break_before_work = break_periods.front();
            CHECK_EQ(break_before_work.begin().time_of_day().total_seconds(), 0);

            break_intervals.push_back(solver->MakeFixedInterval(break_before_work.begin().time_of_day().total_seconds(),
                                                                break_before_work.duration().total_seconds(),
                                                                GetBreakLabel(carer, BreakType::BEFORE_WORKDAY)));

            const auto last_break = break_periods.size() - 1;
            for (auto break_index = 1; break_index < last_break; ++break_index) {
                break_intervals.push_back(CreateBreakInterval(solver,
                                                              break_periods[break_index],
                                                              SolverWrapper::GetBreakLabel(carer, BreakType::BREAK)));
            }

            const auto &break_after_work = break_periods.back();
            CHECK_EQ(break_after_work.end().time_of_day().total_seconds(), 0);

            break_intervals.push_back(solver->MakeFixedInterval(break_after_work.begin().time_of_day().total_seconds(),
                                                                break_after_work.duration().total_seconds(),
                                                                GetBreakLabel(carer, BreakType::AFTER_WORKDAY)));
        } else {
            for (const auto &break_period : break_periods) {
                break_intervals.push_back(CreateBreakInterval(solver,
                                                              break_period,
                                                              SolverWrapper::GetBreakLabel(carer, BreakType::BREAK)));
            }
        }

        return break_intervals;
    }

    std::string SolverWrapper::GetBreakLabel(const rows::Carer &carer,
                                             SolverWrapper::BreakType break_type) {
        switch (break_type) {
            case BreakType::BEFORE_WORKDAY:
                return (boost::format("NodeToCarer '%1%' before workday") % carer).str();
            case BreakType::AFTER_WORKDAY:
                return (boost::format("NodeToCarer '%1%' after workday") % carer).str();
            case BreakType::BREAK:
                return (boost::format("NodeToCarer '%1%' break") % carer).str();
            default:
                throw std::domain_error((boost::format("Handling label '%1%' is not implemented") % carer).str());
        }
    }

    void SolverWrapper::DisplayPlan(const operations_research::RoutingModel &model,
                                    const operations_research::Assignment &solution) {
        operations_research::RoutingDimension const *time_dimension = model.GetMutableDimension(TIME_DIMENSION);

        std::stringstream out;
        out << GetDescription(model, solution);

        for (int route_number = 0; route_number < model.vehicles(); ++route_number) {
            int64 order = model.Start(route_number);
            out << boost::format("Route %1%: ") % route_number;

            if (model.IsEnd(solution.Value(model.NextVar(order)))) {
                out << "Empty" << std::endl;
            } else {
                while (true) {
                    operations_research::IntVar *const time_var = time_dimension->CumulVar(order);
                    if (model.IsEnd(order)) {
                        out << boost::format("%1% Time(%2%, %3%)")
                               % order
                               % solution.Min(time_var)
                               % solution.Max(time_var);
                        break;
                    }

                    operations_research::IntVar *const slack_var = time_dimension->SlackVar(order);
                    if (slack_var != nullptr && solution.Contains(slack_var)) {
                        out << boost::format("%1% Time(%2%, %3%) Slack(%4%, %5%) -> ")
                               % order
                               % solution.Min(time_var) % solution.Max(time_var)
                               % solution.Min(slack_var) % solution.Max(slack_var);
                    }

                    order = solution.Value(model.NextVar(order));
                }
                out << std::endl;
            }
        }

        LOG(INFO) << out.rdbuf();
    }

    operations_research::RoutingSearchParameters SolverWrapper::CreateSearchParameters() {
        operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
        parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);

        static const auto USE_ADVANCED_SEARCH = true;
        parameters.mutable_local_search_operators()->set_use_cross(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_extended_swap_active(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_full_path_lns(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_inactive_lns(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_lin_kernighan(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_make_chain_inactive(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_make_active(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_make_inactive(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_relocate_and_make_active(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_two_opt(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_or_opt(USE_ADVANCED_SEARCH);

        parameters.mutable_local_search_operators()->set_use_path_lns(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_relocate_pair(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_relocate(USE_ADVANCED_SEARCH);

        parameters.mutable_local_search_operators()->set_use_swap_active(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_cross_exchange(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_swap_active(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_relocate_and_make_active(USE_ADVANCED_SEARCH);

        return parameters;
    }

    const operations_research::RoutingSearchParameters &SolverWrapper::parameters() const {
        return parameters_;
    }

    const Location &SolverWrapper::depot() const {
        return depot_;
    }

    std::vector<std::vector<operations_research::RoutingModel::NodeIndex>>
    SolverWrapper::GetRoutes(const rows::Solution &solution, const operations_research::RoutingModel &model) const {
        std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> routes;
        std::unordered_set<operations_research::RoutingModel::NodeIndex> used_nodes;

        for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto carer = Carer(vehicle);

            std::vector<operations_research::RoutingModel::NodeIndex> route;

            const auto local_route = solution.GetRoute(carer);
            for (const auto &visit : local_route.visits()) {
                if (!Contains(visit.calendar_visit().get())) {
                    continue;
                }

                const auto &visit_nodes = GetNodes(visit);
                auto inserted = false;
                for (const auto &node : visit_nodes) {
                    if (used_nodes.find(node) != std::cend(used_nodes)) {
                        continue;
                    }

                    route.push_back(node);
                    inserted = used_nodes.insert(node).second;
                    break;
                }

                DCHECK(inserted);
            }

            routes.emplace_back(std::move(route));
        }
        return routes;
    }

    rows::Solution SolverWrapper::ResolveValidationErrors(const rows::Solution &solution,
                                                          const operations_research::RoutingModel &model) {
        static const rows::SimpleRouteValidatorWithTimeWindows validator{};

        VLOG(1) << "Starting validation of the solution for warm start...";

        const auto start_error_resolution = std::chrono::high_resolution_clock::now();
        rows::Solution solution_to_use{solution};
        while (true) {
            std::vector<std::unique_ptr<rows::RouteValidatorBase::ValidationError>> validation_errors;
            std::vector<rows::Route> routes;
            for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
                const auto carer = Carer(vehicle);
                routes.push_back(solution_to_use.GetRoute(carer));
            }

            validation_errors = validator.ValidateAll(routes, problem_, *this);
            if (VLOG_IS_ON(2)) {
                for (const auto &error_ptr : validation_errors) {
                    VLOG(2) << *error_ptr;
                }
            }

            if (validation_errors.empty()) {
                break;
            }

            solution_to_use = Resolve(solution_to_use, validation_errors);
        }
        const auto end_error_resolution = std::chrono::high_resolution_clock::now();

        if (VLOG_IS_ON(1)) {
            static const auto &is_assigned = [](const rows::ScheduledVisit &visit) -> bool {
                return visit.carer().is_initialized();
            };
            const auto initial_size = std::count_if(std::cbegin(solution.visits()),
                                                    std::cend(solution.visits()),
                                                    is_assigned);
            const auto reduced_size = std::count_if(std::cbegin(solution_to_use.visits()),
                                                    std::cend(solution_to_use.visits()),
                                                    is_assigned);
            VLOG_IF(1, initial_size != reduced_size)
            << boost::format("Removed %1% visit assignments due to constrain violations.")
               % (initial_size - reduced_size);
        }
        VLOG(1) << boost::format("Validation of the solution for warm start completed in %1% seconds")
                   % std::chrono::duration_cast<std::chrono::seconds>(
                end_error_resolution - start_error_resolution).count();

        return solution_to_use;
    }

    rows::Solution SolverWrapper::Resolve(const rows::Solution &solution,
                                          const std::vector<std::unique_ptr<rows::RouteValidatorBase::ValidationError>> &validation_errors) const {
        std::unordered_set<ScheduledVisit> visits_to_ignore;
        std::unordered_set<ScheduledVisit> visits_to_release;
        std::unordered_set<ScheduledVisit> visits_to_move;

        for (const auto &error : validation_errors) {
            switch (error->error_code()) {
                case RouteValidatorBase::ErrorCode::MOVED: {
                    const auto &error_to_use = dynamic_cast<const rows::RouteValidatorBase::ScheduledVisitError &>(*error);
                    visits_to_move.insert(error_to_use.visit());
                    break;
                }
                case RouteValidatorBase::ErrorCode::MISSING_INFO:
                case RouteValidatorBase::ErrorCode::ORPHANED: {
                    const auto &error_to_use = dynamic_cast<const rows::RouteValidatorBase::ScheduledVisitError &>(*error);
                    visits_to_ignore.insert(error_to_use.visit());
                    break;
                }
                case RouteValidatorBase::ErrorCode::ABSENT_CARER:
                case RouteValidatorBase::ErrorCode::BREAK_VIOLATION:
                case RouteValidatorBase::ErrorCode::NOT_ENOUGH_CARERS:
                case RouteValidatorBase::ErrorCode::LATE_ARRIVAL: {
                    const auto &error_to_use = dynamic_cast<const rows::RouteValidatorBase::ScheduledVisitError &>(*error);
                    visits_to_release.insert(error_to_use.visit());
                    break;
                }
                case RouteValidatorBase::ErrorCode::TOO_MANY_CARERS:
                    continue; // is handled separately after all other problems are treated
                default:
                    throw util::ApplicationError(
                            (boost::format("Error code %1% ignored") % error->error_code()).str(),
                            util::ErrorCode::ERROR);
            }
        }
        for (const auto &error : validation_errors) {
            if (error->error_code() == RouteValidatorBase::ErrorCode::TOO_MANY_CARERS) {
                const auto &error_to_use = dynamic_cast<const rows::RouteValidatorBase::RouteConflictError &>(*error);

                const auto &calendar_visit = error_to_use.visit();
                const auto route_end_it = std::end(error_to_use.routes());
                auto route_it = std::begin(error_to_use.routes());

                const auto &predicate = [&calendar_visit](const rows::ScheduledVisit &visit) -> bool {
                    const auto &local_calendar_visit = visit.calendar_visit();
                    return local_calendar_visit.is_initialized() && local_calendar_visit.get() == calendar_visit;
                };

                for (; route_it != route_end_it; ++route_it) {
                    const auto visit_route_it = std::find_if(std::begin(route_it->visits()),
                                                             std::end(route_it->visits()),
                                                             predicate);
                    CHECK(visit_route_it != std::end(route_it->visits()));
                    visits_to_release.insert(*visit_route_it);
                }
            }
        }

        auto released_visits = 0u;
        std::vector<ScheduledVisit> visits_to_use;
        for (const auto &visit : solution.visits()) {
            ScheduledVisit visit_to_use{visit};
            if (visits_to_release.find(visit_to_use) != std::end(visits_to_release)) {
                boost::optional<rows::Carer> &carer = visit_to_use.carer();
                const auto &carer_id = carer.get().sap_number();
                carer.reset();
                released_visits++;
                VLOG(1) << "Carer " << carer_id << " removed from visit: " << visit_to_use;
            } else if (visits_to_ignore.find(visit_to_use) != std::end(visits_to_ignore)) {
                visit_to_use.type(ScheduledVisit::VisitType::INVALID);
                VLOG(1) << "Visit " << visit_to_use << " is ignored due to errors";
            } else if (visits_to_move.find(visit_to_use) != std::end(visits_to_move)) {
                visit_to_use.type(ScheduledVisit::VisitType::MOVED);
                VLOG(1) << "Visit " << visit_to_use << " is moved";
            }
            visits_to_use.emplace_back(visit_to_use);
        }

        DCHECK_EQ(visits_to_release.size(), released_visits);

        return rows::Solution(std::move(visits_to_use));
    }

    bool SolverWrapper::HasTimeWindows() const {
        return visit_time_window_.ticks() != 0;
    }

    int64 SolverWrapper::GetBeginVisitWindow(boost::posix_time::time_duration value) const {
        return GetBeginWindow(value, visit_time_window_);
    }

    int64 SolverWrapper::GetEndVisitWindow(boost::posix_time::time_duration value) const {
        return GetEndWindow(value, visit_time_window_);
    }

    int64 SolverWrapper::GetBeginBreakWindow(boost::posix_time::time_duration value) const {
        return GetBeginWindow(value, break_time_window_);
    }

    int64 SolverWrapper::GetEndBreakWindow(boost::posix_time::time_duration value) const {
        return GetEndWindow(value, break_time_window_);
    }

    int64 SolverWrapper::GetBeginWindow(boost::posix_time::time_duration value,
                                        boost::posix_time::time_duration window_size) const {
        return std::max((value - window_size).total_seconds(), 0);
    }

    int64 SolverWrapper::GetEndWindow(boost::posix_time::time_duration value,
                                      boost::posix_time::time_duration window_size) const {
        return std::min(static_cast<int64>((value + window_size).total_seconds()), SECONDS_IN_DAY);
    }

    const Problem &SolverWrapper::problem() const {
        return problem_;
    }

    std::string SolverWrapper::GetDescription(const operations_research::RoutingModel &model,
                                              const operations_research::Assignment &solution) {
        static const SolutionValidator route_validator{};

        SolverWrapper::Statistics stats;

        operations_research::RoutingDimension const *time_dimension = model.GetMutableDimension(TIME_DIMENSION);

        if (solution.ObjectiveBound()) {
            stats.Cost = solution.ObjectiveValue();
        } else {
            stats.Cost = solution.ObjectiveMax();
        }

        boost::accumulators::accumulator_set<double,
                boost::accumulators::stats<
                        boost::accumulators::tag::mean,
                        boost::accumulators::tag::median,
                        boost::accumulators::tag::variance> > carer_work_stats;

        boost::posix_time::time_duration total_available_time;
        boost::posix_time::time_duration total_work_time;

        auto total_errors = 0;
        std::vector<std::pair<rows::Route, RouteValidatorBase::ValidationResult>> route_pairs;
        for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            auto carer = Carer(vehicle);
            std::vector<rows::ScheduledVisit> carer_visits;

            auto order = model.Start(vehicle);
            while (!model.IsEnd(order)) {
                if (!model.IsStart(order)) {
                    const auto visit_index = model.IndexToNode(order);
                    DCHECK_NE(visit_index, DEPOT);
                    carer_visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN, carer, NodeToVisit(visit_index));
                }

                order = solution.Value(model.NextVar(order));
            }

            rows::Route route{carer, carer_visits};
            VLOG(2) << "Route: " << vehicle;
            RouteValidatorBase::ValidationResult validation_result = route_validator.Validate(vehicle,
                                                                                              solution,
                                                                                              model,
                                                                                              *this);

            if (validation_result.error()) {
                ++total_errors;
            } else {
                const auto &metrics = validation_result.metrics();
                const auto work_time = metrics.available_time() - metrics.idle_time();
                if (metrics.available_time().total_seconds() == 0) {
                    continue;
                }

                const auto relative_utilization = static_cast<double>(work_time.total_seconds())
                                                  / metrics.available_time().total_seconds();
                DCHECK_GE(relative_utilization, 0.0);
                carer_work_stats(relative_utilization);

                total_available_time += metrics.available_time();
                total_work_time += work_time;
            }

            route_pairs.emplace_back(std::move(route), std::move(validation_result));
        }

        if (total_errors > 0) {
            VLOG(2) << "Total validation errors: " << total_errors;
            int vehicle = 0;
            for (const auto &route_pair : route_pairs) {
                if (route_pair.second.error()) {
                    VLOG(2) << "Route " << vehicle << " error: " << *route_pair.second.error();
                }
                ++vehicle;
            }

            LOG(FATAL) << "Total validation errors: " << total_errors;
        } else {
            VLOG(2) << "No validation errors";
        }

        stats.CarerUtility.Mean = boost::accumulators::mean(carer_work_stats);
        stats.CarerUtility.Median = boost::accumulators::median(carer_work_stats);
        stats.CarerUtility.Stddev = sqrt(boost::accumulators::variance(carer_work_stats));
        stats.CarerUtility.TotalMean = (static_cast<double>(total_work_time.total_seconds()) /
                                        total_available_time.total_seconds());

        stats.DroppedVisits = 0;
        for (int order = 1; order < model.nodes(); ++order) {
            if (solution.Value(model.NextVar(order)) == order) {
                ++stats.DroppedVisits;
            }
        }
        stats.TotalVisits = model.nodes() - 1; // remove depot

        return stats.RenderDescription();
    }

    int SolverWrapper::Vehicle(const rows::Carer &carer) const {
        int vehicle_number = 0;
        for (const auto &carer_diary_pairs : problem_.carers()) {
            if (carer_diary_pairs.first == carer) {
                return vehicle_number;
            }

            ++vehicle_number;
        }

        throw util::ApplicationError(
                (boost::format("Carer %1% not found in the definition of the problem") % carer).str(),
                util::ErrorCode::ERROR);
    }

    int SolverWrapper::vehicles() const {
        return static_cast<int>(problem_.carers().size());
    }

    int SolverWrapper::nodes() const {
        return static_cast<int>(visit_by_node_.size());
    }

    bool SolverWrapper::Contains(const CalendarVisit &visit) const {
        return visit_index_.find(visit) != std::cend(visit_index_);
    }

    const std::unordered_set<operations_research::RoutingModel::NodeIndex> &
    SolverWrapper::GetNodes(const CalendarVisit &visit) const {
        const auto find_it = visit_index_.find(visit);
        CHECK(find_it != std::cend(visit_index_));
        CHECK(!find_it->second.empty());
        return find_it->second;
    }

    const std::unordered_set<operations_research::RoutingModel::NodeIndex> &
    SolverWrapper::GetNodes(const ScheduledVisit &visit) const {
        const auto &calendar_visit = visit.calendar_visit().get();
        return GetNodes(calendar_visit);
    }

    const CalendarVisit &
    SolverWrapper::NodeToVisit(const operations_research::RoutingModel::NodeIndex &node) const {
        DCHECK_NE(DEPOT, node);

        return visit_by_node_.at(static_cast<std::size_t>(node.value()));
    }

    int64 SolverWrapper::GetAdjustedWorkdayStart(boost::posix_time::time_duration start_time) const {
        if (begin_end_work_day_adjustment_.is_special()) {
            return start_time.total_seconds();
        }
        return std::max(static_cast<int>((start_time - begin_end_work_day_adjustment_).total_seconds()), 0);
    }

    int64 SolverWrapper::GetAdjustedWorkdayFinish(boost::posix_time::time_duration finish_time) const {
        if (begin_end_work_day_adjustment_.is_special()) {
            return finish_time.total_seconds();
        }
        return std::min(static_cast<int64>((finish_time + begin_end_work_day_adjustment_).total_seconds()),
                        SECONDS_IN_DAY);
    }

    void SolverWrapper::OnConfigureModel(const operations_research::RoutingModel &model) {
        VLOG(1) << "Computing missing entries of the distance matrix...";
        const auto start_time_distance_computation = std::chrono::high_resolution_clock::now();
        const auto distance_pairs = location_container_.ComputeDistances();

        const auto end_time_distance_computation = std::chrono::high_resolution_clock::now();
        VLOG(1) << boost::format("Computed distances between %1% locations in %2% seconds")
                   % distance_pairs
                   % std::chrono::duration_cast<std::chrono::seconds>(end_time_distance_computation
                                                                      - start_time_distance_computation).count();

        if (model.nodes() == 0) {
            throw util::ApplicationError("Model contains no visits.", util::ErrorCode::ERROR);
        }

        const auto schedule_day = GetScheduleDate();
        if (model.nodes() > 1) {
            for (operations_research::RoutingModel::NodeIndex visit_node{2}; visit_node < model.nodes(); ++visit_node) {
                const auto &visit = NodeToVisit(visit_node);
                if (visit.datetime().date() != schedule_day) {
                    throw util::ApplicationError("Visits span across multiple days.", util::ErrorCode::ERROR);
                }
            }
        }
    }

    boost::posix_time::time_duration SolverWrapper::GetAdjustment() const {
        if (begin_end_work_day_adjustment_.is_special()) {
            return {};
        }
        return begin_end_work_day_adjustment_;
    }

    boost::gregorian::date SolverWrapper::GetScheduleDate() const {
        return NodeToVisit(operations_research::RoutingModel::NodeIndex{1}).datetime().date();
    }

    int64 SolverWrapper::GetDroppedVisitPenalty(const operations_research::RoutingModel &model) {
        std::vector<int64> distances;
        distances.reserve(static_cast<std::size_t>((model.nodes() - 1) * model.nodes()));

        const auto depot_node = model.IndexToNode(model.GetDepot());
        const auto max_node = model.nodes();
        for (operations_research::RoutingModel::NodeIndex source{0}; source < max_node; ++source) {
            for (operations_research::RoutingModel::NodeIndex destination{0}; destination < max_node; ++destination) {
                if (source == destination || source == depot_node || destination == depot_node) {
                    continue;
                }

                distances.push_back(Distance(source, destination));
            }
        }

        if (distances.empty()) {
            return std::numeric_limits<int64>::max();
        }

        std::sort(std::begin(distances), std::end(distances));
        const auto distance_pos = static_cast<std::size_t>(distances.size() * 0.6);
        CHECK_LT(distance_pos, distances.size());
        return distances.at(distance_pos);
    }

    std::string rows::SolverWrapper::GetModelStatus(int status) {
        switch (status) {
            case operations_research::RoutingModel::Status::ROUTING_FAIL:
                return "ROUTING_FAIL";
            case operations_research::RoutingModel::Status::ROUTING_FAIL_TIMEOUT:
                return "ROUTING_FAIL_TIMEOUT";
            case operations_research::RoutingModel::Status::ROUTING_INVALID:
                return "ROUTING_INVALID";
            case operations_research::RoutingModel::Status::ROUTING_NOT_SOLVED:
                return "ROUTING_NOT_SOLVED";
            case operations_research::RoutingModel::Status::ROUTING_SUCCESS:
                return "ROUTING_SUCCESS";
        }
    }

    std::pair<operations_research::RoutingModel::NodeIndex,
            operations_research::RoutingModel::NodeIndex> SolverWrapper::GetNodePair(
            const rows::CalendarVisit &visit) const {
        const auto &nodes = GetNodes(visit);
        CHECK_EQ(nodes.size(), 2);

        const auto first_node = *std::begin(nodes);
        const auto second_node = *std::next(std::begin(nodes));
        auto first_node_to_use = first_node;
        auto second_node_to_use = second_node;
        if (first_node_to_use > second_node_to_use) {
            std::swap(first_node_to_use, second_node_to_use);
        }

        return std::make_pair(first_node_to_use, second_node_to_use);
    }

    SolverWrapper::LocalServiceUser::LocalServiceUser()
            : LocalServiceUser(ExtendedServiceUser(), 1) {}


    int64 SolverWrapper::LocalServiceUser::Preference(const rows::Carer &carer) const {
        return static_cast<int64>(service_user_.preference(carer) * 100 / visit_count_);
    }

    SolverWrapper::LocalServiceUser::LocalServiceUser(const rows::ExtendedServiceUser &service_user,
                                                      int64 visit_count)
            : service_user_(service_user),
              visit_count_(visit_count) {}

    bool SolverWrapper::LocalServiceUser::IsPreferred(const rows::Carer &carer) const {
        return service_user_.IsPreferred(carer);
    }

    const rows::ExtendedServiceUser &SolverWrapper::LocalServiceUser::service_user() const {
        return service_user_;
    }

    int64 SolverWrapper::LocalServiceUser::visit_count() const {
        return visit_count_;
    }

    std::string SolverWrapper::Statistics::RenderDescription() const {
        return (boost::format("Cost: %1%\nErrors: %2%\nDropped visits: %3%\nTotal visits: %4%\n"
                              "Carer utility: mean: %5% median: %6% stddev: %7% total ratio: %8%\n")
                % Cost
                % Errors
                % DroppedVisits
                % TotalVisits
                % CarerUtility.Mean
                % CarerUtility.Median
                % CarerUtility.Stddev
                % CarerUtility.TotalMean).str();
    }
}
