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


namespace rows {

    const operations_research::RoutingModel::NodeIndex SolverWrapper::DEPOT{0};

    const int64 SolverWrapper::SECONDS_IN_DAY = 24 * 3600;

    const std::string SolverWrapper::TIME_DIMENSION{"Time"};

    const int64 SolverWrapper::CARE_CONTINUITY_MAX = 10000;

    const std::string SolverWrapper::CARE_CONTINUITY_DIMENSION{"CareContinuity"};

    SolverWrapper::SolverWrapper(const rows::Problem &problem, osrm::EngineConfig &config)
            : SolverWrapper(problem, [](const rows::Problem &problem) -> std::vector<Location> {
        std::unordered_set<Location> locations;
        for (const auto &visit : problem.visits()) {
            const auto &location_opt = visit.location();
            if (location_opt) {
                locations.insert(location_opt.get());
            }
        }

        return {std::cbegin(locations), std::cend(locations)};
    }(problem), config) {}

    SolverWrapper::SolverWrapper(const rows::Problem &problem,
                                 const std::vector<rows::Location> &locations,
                                 osrm::EngineConfig &config)
            : problem_(problem),
              depot_(Location::GetCentralLocation(std::cbegin(locations), std::cend(locations))),
              depot_service_user_(),
              visit_time_window_(boost::posix_time::minutes(30)),
              location_container_(std::cbegin(locations), std::cend(locations), config),
              parameters_(CreateSearchParameters()),
              visit_index_(),
              visit_by_node_(),
              service_users_(),
              care_continuity_(),
              care_continuity_metrics_() {

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
            DCHECK_GT(visit_count, 0);
            const auto insert_it = service_users_.insert(
                    std::make_pair(service_user, LocalServiceUser(service_user, visit_count)));
            DCHECK(insert_it.second);

            care_continuity_.insert(std::make_pair(service_user, nullptr));
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

    int64 SolverWrapper::ServicePlusTravelTime(operations_research::RoutingModel::NodeIndex from,
                                               operations_research::RoutingModel::NodeIndex to) {
        if (from == DEPOT) {
            return 0;
        }

        const auto visit = NodeToVisit(from);
        const auto service_time = visit.duration().total_seconds();
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

    std::vector<operations_research::IntervalVar *> SolverWrapper::CreateBreakIntervals(
            operations_research::Solver *const solver,
            const rows::Carer &carer,
            const rows::Diary &diary) const {
        std::vector<operations_research::IntervalVar *> result;

        const auto &events = diary.events();
        if (!events.empty()) {
            auto event_it = std::begin(events);
            const auto event_it_end = std::end(events);

            result.push_back(solver->MakeFixedInterval(0,
                                                       event_it->begin().time_of_day().total_seconds(),
                                                       GetBreakLabel(carer, BreakType::BEFORE_WORKDAY)));

            auto prev_event_it = event_it++;
            while (event_it != event_it_end) {
                result.push_back(solver->MakeFixedInterval(
                        prev_event_it->end().time_of_day().total_seconds(),
                        (event_it->begin() - prev_event_it->end()).total_seconds(),
                        GetBreakLabel(carer, BreakType::BREAK)));

                prev_event_it = event_it++;
            }

            result.push_back(solver->MakeFixedInterval(prev_event_it->end().time_of_day().total_seconds(),
                                                       (boost::posix_time::hours(24)
                                                        - prev_event_it->end().time_of_day()).total_seconds(),
                                                       GetBreakLabel(carer, BreakType::AFTER_WORKDAY)));
        }

        // TODO: investigation
        for (const auto &interval : result) {
            LOG(INFO) << boost::format("%1% [%2%,%3%], [%4%,%5%]")
                         % interval->name()
                         % interval->StartMin()
                         % interval->StartMax()
                         % interval->EndMin()
                         % interval->EndMax();
        }

        return result;
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
        const auto stats = CalculateStats(model, solution);
        operations_research::RoutingDimension const *time_dimension = model.GetMutableDimension(TIME_DIMENSION);

        std::stringstream out;

        out << boost::format("Cost: %1$g") % stats.Cost
            << std::endl;

        if (stats.Errors > 0) {
            out << boost::format("Solution has %1% validation errors") % stats.Errors << std::endl;
        }

        out << boost::format("NodeToCarer utilization: mean: %1% median %2% stddev %3% total ratio %4%")
               % stats.CarerUtility.Mean
               % stats.CarerUtility.Median
               % stats.CarerUtility.Stddev
               % stats.CarerUtility.TotalMean
            << std::endl;

        if (stats.DroppedVisits == 0) {
            out << "No dropped visits";
        } else {
            out << boost::format("Dropped visits: %1%")
                   % stats.DroppedVisits;
        }
        out << std::endl;

        out << boost::format("Continuity of care average: %1% mean: %2% stddev: %3%")
               % stats.CareContinuity.Mean
               % stats.CareContinuity.Median
               % stats.CareContinuity.Stddev
            << std::endl;

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

    operations_research::IntervalVar *SolverWrapper::CreateBreakWithTimeWindows(operations_research::Solver *solver,
                                                                                const boost::posix_time::time_duration &start_time,
                                                                                const boost::posix_time::time_duration &duration,
                                                                                const std::string &label) const {
        static const auto IS_OPTIONAL = false;
        return solver->MakeFixedDurationIntervalVar(
                GetBeginWindow(start_time),
                GetEndWindow(start_time),
                duration.total_seconds(),
                IS_OPTIONAL,
                label);
    }

    operations_research::RoutingSearchParameters SolverWrapper::CreateSearchParameters() const {
        operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
        parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
        parameters.set_solution_limit(256);
        parameters.set_time_limit_ms(boost::posix_time::minutes(3).total_milliseconds());

        static const auto USE_ADVANCED_SEARCH = true;
        parameters.set_use_light_propagation(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_path_lns(USE_ADVANCED_SEARCH);
        parameters.mutable_local_search_operators()->set_use_inactive_lns(USE_ADVANCED_SEARCH);
        return parameters;
    }

    void SolverWrapper::ConfigureModel(operations_research::RoutingModel &model) {
        static const auto START_FROM_ZERO_TIME = true;
        static const auto START_FROM_ZERO_SERVICE_SATISFACTION = true;

        VLOG(1) << "Time window width: " << visit_time_window_;

        model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(this, &rows::SolverWrapper::Distance));

        model.AddDimension(NewPermanentCallback(this, &rows::SolverWrapper::ServicePlusTravelTime),
                           SECONDS_IN_DAY,
                           SECONDS_IN_DAY,
                           START_FROM_ZERO_TIME,
                           TIME_DIMENSION);

        operations_research::RoutingDimension *time_dimension
                = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

        if (model.nodes() == 0) {
            throw util::ApplicationError("Model contains no visits.", util::ErrorCode::ERROR);
        }

        const auto schedule_day = NodeToVisit(operations_research::RoutingModel::NodeIndex{1}).datetime().date();
        if (model.nodes() > 1) {
            for (operations_research::RoutingModel::NodeIndex visit_node{2}; visit_node < model.nodes(); ++visit_node) {
                const auto &visit = NodeToVisit(visit_node);
                if (visit.datetime().date() != schedule_day) {
                    throw util::ApplicationError("Visits span across multiple days.", util::ErrorCode::ERROR);
                }
            }
        }

        auto total_multiple_carer_visits = 0;
        time_dimension->CumulVar(model.NodeToIndex(DEPOT))->SetRange(0, SECONDS_IN_DAY);
        std::set<operations_research::RoutingModel::NodeIndex> covered_nodes;
        covered_nodes.insert(DEPOT);

        // FIX set min and max value for all vehicles
        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            time_dimension->CumulVar(model.Start(vehicle))->SetRange(0, SECONDS_IN_DAY);
            time_dimension->CumulVar(model.End(vehicle))->SetRange(0, SECONDS_IN_DAY);
        }

        // visit that needs multiple carers is referenced by multiple nodes
        // all such nodes must be either performed or unperformed
        for (const auto &visit_index_pair : visit_index_) {
            const auto visit_start = visit_index_pair.first.datetime().time_of_day();
            LOG(INFO) << visit_start;

            std::vector<operations_research::IntVar *> start_visit_vars;
            std::vector<operations_research::IntVar *> active_visit_vars;
            for (const auto &visit_node : visit_index_pair.second) {
                covered_nodes.insert(visit_node);
                const auto visit_index = model.NodeToIndex(visit_node);
                if (HasTimeWindows()) {
                    time_dimension->CumulVar(visit_index)->SetRange(GetBeginWindow(visit_start),
                                                                    GetEndWindow(visit_start));

                    const auto start_window = boost::posix_time::seconds(GetBeginWindow(visit_start));
                    const auto end_window = boost::posix_time::seconds(GetEndWindow(visit_start));
                    DCHECK_LT(start_window, end_window);
                    DCHECK_EQ((start_window + end_window) / 2, visit_start);
                } else {
                    time_dimension->CumulVar(visit_index)->SetValue(visit_start.total_seconds());
                }
                model.AddToAssignment(time_dimension->SlackVar(visit_index));

                start_visit_vars.push_back(time_dimension->CumulVar(visit_index));
                active_visit_vars.push_back(model.ActiveVar(visit_index));
            }

            const auto visit_index_size = start_visit_vars.size();
            if (visit_index_size > 1) {
//                for (auto visit_index = 1; visit_index < visit_index_size; ++visit_index) {
//                    const auto start_time_constraint = model.solver()->MakeEquality(start_visit_vars[0],
//                                                                                    start_visit_vars[visit_index]);
//                    solver->AddConstraint(start_time_constraint);

//                    const auto active_constraint = model.solver()->MakeEquality(active_visit_vars[0],
//                                                                                active_visit_vars[visit_index]);
//                    solver->AddConstraint(active_constraint);
//                }

                // TODO: create and add constraint
                ++total_multiple_carer_visits;
            }
        }

        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto &carer = Carer(vehicle);
            const auto &diary = problem_.diary(carer, schedule_day);

            const auto breaks = CreateBreakIntervals(model.solver(), carer, diary.get());
            auto solver_ptr = model.solver();
            solver_ptr->AddConstraint(solver_ptr->RevAlloc(new BreakConstraint(time_dimension, vehicle, breaks)));
//            time_dimension->SetBreakIntervalsOfVehicle(breaks, vehicle);
        }

        LOG(INFO) << "Total multiple carer visits: " << total_multiple_carer_visits;
        LOG(INFO) << covered_nodes.size();

        for (const auto &carer_pair :problem_.carers()) {
            care_continuity_metrics_.emplace_back(*this, carer_pair.first);
        }

        std::vector<operations_research::RoutingModel::NodeEvaluator2 *> care_continuity_evaluators;
        for (const auto &carer_metrics :care_continuity_metrics_) {
            care_continuity_evaluators.
                    push_back(
                    NewPermanentCallback(&carer_metrics, &CareContinuityMetrics::operator()));
        }

        model.AddDimensionWithVehicleTransits(care_continuity_evaluators,
                                              0,
                                              CARE_CONTINUITY_MAX,
                                              START_FROM_ZERO_SERVICE_SATISFACTION,
                                              CARE_CONTINUITY_DIMENSION);

        operations_research::RoutingDimension const *care_continuity_dimension = model.GetMutableDimension(
                rows::SolverWrapper::CARE_CONTINUITY_DIMENSION);

        // minimize time variables
        for (auto variable_index = 0; variable_index < model.Size(); ++variable_index) {
            model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(variable_index));
        }

        // minimize route duration
        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(model.Start(vehicle)));
            model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(model.End(vehicle)));
        }

        // define and maximize the service satisfaction
        for (const auto &service_user :problem_.service_users()) {
            std::vector<operations_research::IntVar *> service_user_visits;

            for (const auto &visit :problem_.visits()) {
                if (visit.service_user() != service_user) {
                    continue;
                }

                const auto visit_it = visit_index_.find(visit);
                DCHECK(visit_it != std::end(visit_index_));
                for (const auto &visit_node : visit_it->second) {
                    service_user_visits.push_back(
                            care_continuity_dimension->TransitVar(model.NodeToIndex(visit_node)));
                }
            }

            auto find_it = care_continuity_.find(service_user);
            DCHECK(find_it != std::end(care_continuity_));

            const auto care_satisfaction = model.solver()->MakeSum(service_user_visits)->Var();
            find_it->second = care_satisfaction;

            model.AddToAssignment(care_satisfaction);
            model.AddVariableMaximizedByFinalizer(care_satisfaction);
        }

        // Adding penalty costs to allow skipping orders.
        const int64 kPenalty = 10000000;
        const operations_research::RoutingModel::NodeIndex kFirstNodeAfterDepot(1);
        for (operations_research::RoutingModel::NodeIndex order = kFirstNodeAfterDepot;
             order < model.nodes(); ++order) {
            std::vector<operations_research::RoutingModel::NodeIndex> orders(1, order);
            model.AddDisjunction(orders, kPenalty);
        }

        VLOG(1) << "Computing missing entries of the distance matrix...";
        const auto start_time_distance_computation = std::chrono::high_resolution_clock::now();
        const auto distance_pairs = location_container_.ComputeDistances();

        const auto end_time_distance_computation = std::chrono::high_resolution_clock::now();
        VLOG(1) << boost::format("Computed distances between %1% locations in %2% seconds")
                   % distance_pairs
                   % std::chrono::duration_cast<std::chrono::seconds>(end_time_distance_computation
                                                                      - start_time_distance_computation).count();

        VLOG(1) << "Finalizing definition of the routing model...";
        const auto start_time_model_closing = std::chrono::high_resolution_clock::now();

        model.CloseModelWithParameters(parameters_);

        const auto end_time_model_closing = std::chrono::high_resolution_clock::now();
        VLOG(1) << boost::format("Definition of the routing model finalized in %1% seconds")
                   % std::chrono::duration_cast<std::chrono::seconds>(end_time_model_closing
                                                                      - start_time_model_closing).count();
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
                                                          const rows::Problem &problem,
                                                          const operations_research::RoutingModel &model) {
        static const rows::RouteValidator validator{};

        VLOG(1) << "Starting validation of the solution for warm start...";

        const auto start_error_resolution = std::chrono::high_resolution_clock::now();
        rows::Solution solution_to_use{solution};
        while (true) {
            std::vector<std::unique_ptr<rows::RouteValidator::ValidationError>> validation_errors;
            std::vector<rows::Route> routes;
            for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
                const auto carer = Carer(vehicle);
                routes.push_back(solution_to_use.GetRoute(carer));
            }

            validation_errors = validator.Validate(routes, problem, *this);
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
                                          const std::vector<std::unique_ptr<rows::RouteValidator::ValidationError>> &validation_errors) const {
        // TODO: Update resolution logic to reset all visits that have multiple carers in outer routes as well
        std::unordered_set<ScheduledVisit> visits_to_ignore;
        std::unordered_set<ScheduledVisit> visits_to_release;
        std::unordered_set<ScheduledVisit> visits_to_move;

        for (const auto &error : validation_errors) {
            switch (error->error_code()) {
                case RouteValidator::ErrorCode::MOVED: {
                    const auto &error_to_use = dynamic_cast<const rows::RouteValidator::ScheduledVisitError &>(*error);
                    visits_to_move.insert(error_to_use.visit());
                    break;
                }
                case RouteValidator::ErrorCode::MISSING_INFO:
                case RouteValidator::ErrorCode::ORPHANED: {
                    const auto &error_to_use = dynamic_cast<const rows::RouteValidator::ScheduledVisitError &>(*error);
                    visits_to_ignore.insert(error_to_use.visit());
                    break;
                }
                case RouteValidator::ErrorCode::ABSENT_CARER:
                case RouteValidator::ErrorCode::BREAK_VIOLATION:
                case RouteValidator::ErrorCode::LATE_ARRIVAL: {
                    const auto &error_to_use = dynamic_cast<const rows::RouteValidator::ScheduledVisitError &>(*error);
                    visits_to_release.insert(error_to_use.visit());
                    break;
                }
                case RouteValidator::ErrorCode::TOO_MANY_CARERS:
                    continue; // is handled separately after all other problems are treated
                default:
                    throw util::ApplicationError(
                            (boost::format("Error code %1% ignored") % error->error_code()).str(),
                            util::ErrorCode::ERROR);
            }
        }
        for (const auto &error : validation_errors) {
            if (error->error_code() == RouteValidator::ErrorCode::TOO_MANY_CARERS) {
                const auto &error_to_use = dynamic_cast<const rows::RouteValidator::RouteConflictError &>(*error);

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

    int64 SolverWrapper::GetBeginWindow(boost::posix_time::time_duration value) const {
        return std::max((value - visit_time_window_).total_seconds(), 0);
    }

    int64 SolverWrapper::GetEndWindow(boost::posix_time::time_duration value) const {
        return std::min((value + visit_time_window_).total_seconds(), static_cast<int>(SECONDS_IN_DAY));
    }

    const Problem &SolverWrapper::problem() const {
        return problem_;
    }

    SolverWrapper::Statistics SolverWrapper::CalculateStats(const operations_research::RoutingModel &model,
                                                            const operations_research::Assignment &solution) {
        static const rows::RouteValidator route_validator{};

        SolverWrapper::Statistics stats;

        operations_research::RoutingDimension const *time_dimension = model.GetMutableDimension(TIME_DIMENSION);

        stats.Cost = solution.ObjectiveValue();

        boost::accumulators::accumulator_set<double,
                boost::accumulators::stats<
                        boost::accumulators::tag::mean,
                        boost::accumulators::tag::median,
                        boost::accumulators::tag::variance> > carer_work_stats;

        boost::posix_time::time_duration total_available_time;
        boost::posix_time::time_duration total_work_time;

        auto total_errors = 0;
        std::vector<std::pair<rows::Route, RouteValidator::ValidationResult>> route_pairs;
        for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            auto carer = Carer(vehicle);
            std::vector<rows::ScheduledVisit> carer_visits;

            auto order = model.Start(vehicle);
            if (!model.IsEnd(solution.Value(model.NextVar(order)))) {
                while (!model.IsEnd(order)) {
                    const auto visit_index = model.IndexToNode(order);
                    if (visit_index != DEPOT) {
                        carer_visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN,
                                                  carer,
                                                  NodeToVisit(visit_index));
                    }

                    order = solution.Value(model.NextVar(order));
                }
            }

            rows::Route route{carer, carer_visits};
            VLOG(2) << "Route: " << vehicle;
            RouteValidator::ValidationResult validation_result = route_validator.Validate(route, *this);

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
        } else {
            VLOG(2) << "No validation errors";
        }

        VLOG(1) << model.DebugOutputAssignment(solution, "");

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

        boost::accumulators::accumulator_set<double,
                boost::accumulators::stats<
                        boost::accumulators::tag::mean,
                        boost::accumulators::tag::median,
                        boost::accumulators::tag::variance> > care_continuity_stats;

        for (const auto &user_satisfaction_pair : care_continuity_) {
            care_continuity_stats(solution.Value(user_satisfaction_pair.second));
        }
        stats.CareContinuity.Mean = boost::accumulators::mean(care_continuity_stats);
        stats.CareContinuity.Median = boost::accumulators::median(care_continuity_stats);
        stats.CareContinuity.Stddev = sqrt(boost::accumulators::variance(care_continuity_stats));

        return stats;
    }

    operations_research::IntVar const *
    SolverWrapper::CareContinuityVar(const rows::ExtendedServiceUser &service_user) const {
        const auto service_user_it = care_continuity_.find(service_user);
        DCHECK(service_user_it != std::cend(care_continuity_));
        return service_user_it->second;
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

    std::size_t
    SolverWrapper::PartialVisitOperations::operator()(const rows::CalendarVisit &object) const noexcept {
        static const std::hash<rows::ServiceUser> hash_service_user{};
        static const std::hash<boost::posix_time::ptime> hash_date_time{};
        static const std::hash<boost::posix_time::ptime::time_duration_type> hash_duration{};

        std::size_t seed = 0;
        boost::hash_combine(seed, hash_service_user(object.service_user()));
        boost::hash_combine(seed, hash_date_time(object.datetime()));
        boost::hash_combine(seed, hash_duration(object.duration()));
        return seed;
    }

    bool SolverWrapper::PartialVisitOperations::operator()(const rows::CalendarVisit &left,
                                                           const rows::CalendarVisit &right) const noexcept {
        return left.service_user() == right.service_user()
               && left.datetime() == right.datetime()
               && left.duration() == right.duration();
    }

    SolverWrapper::CareContinuityMetrics::CareContinuityMetrics(const SolverWrapper &solver,
                                                                const rows::Carer &carer)
            : values_() {
        const auto visit_it_end = std::cend(solver.visit_index_);
        for (auto visit_it = std::cbegin(solver.visit_index_); visit_it != visit_it_end; ++visit_it) {
            const auto &service_user = solver.User(visit_it->first.service_user());
            if (service_user.IsPreferred(carer)) {
                for (const auto visit_node : visit_it->second) {
                    values_.insert(std::make_pair(visit_node, service_user.Preference(carer)));
                }
            }
        }
    }

    int64 SolverWrapper::CareContinuityMetrics::operator()(operations_research::RoutingModel::NodeIndex from,
                                                           operations_research::RoutingModel::NodeIndex to) const {
        const auto to_it = values_.find(to);
        if (to_it == std::end(values_)) {
            return 0;
        }
        return to_it->second;
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
}
