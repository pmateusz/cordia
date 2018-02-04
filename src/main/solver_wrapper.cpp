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


namespace rows {

    const operations_research::RoutingModel::NodeIndex SolverWrapper::DEPOT{0};

    const int64 SolverWrapper::SECONDS_IN_DAY = 24 * 3600;

    const std::string SolverWrapper::TIME_DIMENSION{"Time"};

    const int64 SolverWrapper::CARE_CONTINUITY_MAX = 10000;

    const std::string SolverWrapper::CARE_CONTINUITY_DIMENSION{"CareContinuity"};

    SolverWrapper::SolverWrapper(const rows::Problem &problem, osrm::EngineConfig &config)
            : SolverWrapper(problem, GetUniqueLocations(problem), config) {}

    SolverWrapper::SolverWrapper(const rows::Problem &problem,
                                 const std::vector<rows::Location> &locations,
                                 osrm::EngineConfig &config)
            : problem_(problem),
              depot_(GetCentralLocation(std::cbegin(locations), std::cend(locations))),
              depot_service_user_(),
              visit_time_window_(boost::posix_time::minutes(30)),
              location_container_(std::cbegin(locations), std::cend(locations), config),
              parameters_(CreateSearchParameters()),
              service_users_(),
              care_continuity_(),
              care_continuity_metrics_() {
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

        return location_container_.Distance(CalendarVisit(from).location().get(),
                                            CalendarVisit(to).location().get());
    }

    boost::posix_time::ptime::time_duration_type SolverWrapper::TravelTime(const Location &from, const Location &to) {
        if (from == depot_ || to == depot_) {
            return boost::posix_time::seconds(0);
        }

        return boost::posix_time::seconds(location_container_.Distance(from, to));
    }

    int64 SolverWrapper::ServicePlusTravelTime(operations_research::RoutingModel::NodeIndex from,
                                               operations_research::RoutingModel::NodeIndex to) {
        if (from == DEPOT) {
            return 0;
        }

        const auto visit = CalendarVisit(from);
        const auto service_time = visit.duration();
        const auto travel_time = boost::posix_time::seconds(Distance(from, to));
        return (service_time + travel_time).total_seconds();
    }

    rows::CalendarVisit SolverWrapper::CalendarVisit(const operations_research::RoutingModel::NodeIndex visit) const {
        DCHECK_NE(visit, DEPOT);

        return problem_.visits()[visit.value() - 1];
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

    boost::optional<rows::Diary>
    SolverWrapper::Diary(const operations_research::RoutingModel::NodeIndex carer, boost::gregorian::date date) const {
        const auto &carer_pairs = problem_.carers();
        const auto carer_pair = carer_pairs.at(static_cast<std::size_t>(carer.value()));
        return FindDiaryOrNone(carer_pair.second, date);
    }

    boost::optional<rows::Diary> SolverWrapper::Diary(const rows::Carer &carer, boost::gregorian::date date) const {
        const auto &carer_pairs = problem_.carers();
        const auto find_carer_pair_it = std::find_if(std::cbegin(carer_pairs),
                                                     std::cend(carer_pairs),
                                                     [&carer](const std::pair<rows::Carer,
                                                             std::vector<rows::Diary>> &pair) -> bool {
                                                         return pair.first == carer;
                                                     });
        if (find_carer_pair_it == std::cend(carer_pairs)) {
            return boost::none;
        }
        return FindDiaryOrNone(find_carer_pair_it->second, date);
    }

    rows::Carer SolverWrapper::Carer(const operations_research::RoutingModel::NodeIndex carer) const {
        const auto &carer_pair = problem_.carers()[carer.value()];
        return {carer_pair.first};
    }

    const rows::SolverWrapper::LocalServiceUser &SolverWrapper::ServiceUser(
            operations_research::RoutingModel::NodeIndex visit) const {
        if (visit == DEPOT) {
            return depot_service_user_;
        }

        const auto find_it = visit_index_.right.find(visit);
        DCHECK(find_it != std::cend(visit_index_.right));
        return ServiceUser(find_it->second.service_user());
    }

    const rows::SolverWrapper::LocalServiceUser &SolverWrapper::ServiceUser(
            const rows::ServiceUser &service_user) const {
        const auto find_it = service_users_.find(service_user);
        DCHECK(find_it != std::end(service_users_));
        return find_it->second;
    }

    std::vector<operations_research::IntervalVar *> SolverWrapper::Breaks(operations_research::Solver *const solver,
                                                                          const rows::Carer &carer,
                                                                          const rows::Diary &diary) const {
        std::vector<operations_research::IntervalVar *> result;

        boost::posix_time::ptime last_end_time(diary.date());
        boost::posix_time::ptime next_day(diary.date() + boost::gregorian::date_duration(1));

        const auto &events = diary.events();
        if (!events.empty()) {
            auto event_it = std::begin(events);
            const auto event_it_end = std::end(events);

            result.push_back(CreateBreak(solver,
                                         last_end_time.time_of_day(),
                                         event_it->begin().time_of_day(),
                                         GetBreakLabel(carer, BreakType::BEFORE_WORKDAY)));

            auto prev_event_it = event_it++;
            while (event_it != event_it_end) {
                result.push_back(CreateBreak(solver,
                                             prev_event_it->end().time_of_day(),
                                             event_it->begin() - prev_event_it->end(),
                                             GetBreakLabel(carer, BreakType::BREAK)));

                prev_event_it = event_it++;
            }

            result.push_back(CreateBreak(solver,
                                         prev_event_it->end().time_of_day(),
                                         next_day - prev_event_it->end(),
                                         GetBreakLabel(carer, BreakType::AFTER_WORKDAY)));
        }

        return result;
    }

    std::string SolverWrapper::GetBreakLabel(const rows::Carer &carer,
                                             SolverWrapper::BreakType break_type) {
        switch (break_type) {
            case BreakType::BEFORE_WORKDAY:
                return (boost::format("Carer '%1%' before workday") % carer).str();
            case BreakType::AFTER_WORKDAY:
                return (boost::format("Carer '%1%' after workday") % carer).str();
            case BreakType::BREAK:
                return (boost::format("Carer '%1%' break") % carer).str();
            default:
                throw std::domain_error((boost::format("Handling label '%1%' is not implemented") % carer).str());
        }
    }

    int SolverWrapper::VehicleCount() const {
        return static_cast<int>(problem_.carers().size());
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

        out << boost::format("Carer utilization: mean: %1% median %2% stddev %3% total ratio %4%")
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

    operations_research::IntervalVar *SolverWrapper::CreateBreak(operations_research::Solver *const solver,
                                                                 const boost::posix_time::time_duration &start_time,
                                                                 const boost::posix_time::time_duration &duration,
                                                                 const std::string &label) const {
        return solver->MakeFixedInterval(start_time.total_seconds(), duration.total_seconds(), label);
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

    std::vector<rows::Location> SolverWrapper::GetUniqueLocations(const rows::Problem &problem) {
        std::unordered_set<rows::Location> location_set;
        for (const auto &visit : problem.visits()) {
            if (visit.location()) {
                location_set.insert(visit.location().get());
            }
        }
        return std::vector<rows::Location>(std::cbegin(location_set), std::cend(location_set));
    }

    template<typename IteratorType>
    Location SolverWrapper::GetCentralLocation(IteratorType begin_it, IteratorType end_it) {
        std::vector<std::tuple<double, double, double>> points;

        for (auto it = begin_it; it != end_it; ++it) {
            points.push_back(ToCartesianCoordinates(it->latitude(), it->longitude()));
        }

        const auto average_point = GetAveragePoint(points);
        std::pair<osrm::FixedLatitude, osrm::FixedLongitude> average_geo_point = ToGeographicCoordinates(
                average_point);
        return {average_geo_point.first, average_geo_point.second};
    }


    operations_research::RoutingModel::NodeIndex SolverWrapper::Index(const rows::CalendarVisit &visit) const {
        const auto &index_opt = TryIndex(visit);
        if (index_opt.is_initialized()) {
            return index_opt.get();
        }

        throw std::domain_error((boost::format("Visit %1% not found in the index") % visit).str());
    }

    operations_research::RoutingModel::NodeIndex SolverWrapper::Index(const rows::ScheduledVisit &visit) const {
        const auto &calendar_visit = visit.calendar_visit();
        if (calendar_visit) {
            return Index(calendar_visit.get());
        }

        throw std::domain_error((boost::format("Visit %1% do not have a calendar visit") % visit).str());
    }

    boost::optional<operations_research::RoutingModel::NodeIndex> SolverWrapper::TryIndex(
            const rows::CalendarVisit &visit) const {
        const auto find_it = visit_index_.left.find(visit);
        if (find_it != std::end(visit_index_.left)) {
            return boost::make_optional(find_it->second);
        }
        return boost::none;

    }

    boost::optional<operations_research::RoutingModel::NodeIndex> SolverWrapper::TryIndex(
            const rows::ScheduledVisit &visit) const {
        const auto &calendar_visit = visit.calendar_visit();
        if (!calendar_visit.is_initialized()) {
            return boost::none;
        }

        return TryIndex(calendar_visit.get());
    }

    operations_research::RoutingSearchParameters SolverWrapper::CreateSearchParameters() const {
        operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
        parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
        parameters.set_solution_limit(16);
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

        operations_research::RoutingDimension *time_dimension = model.GetMutableDimension(
                rows::SolverWrapper::TIME_DIMENSION);

        if (model.nodes() == 0) {
            throw util::ApplicationError("Model contains no visits.", util::ErrorCode::ERROR);
        }

        const auto schedule_day = CalendarVisit(operations_research::RoutingModel::NodeIndex{1}).datetime().date();
        if (model.nodes() > 1) {
            for (operations_research::RoutingModel::NodeIndex visit_node{2}; visit_node < model.nodes(); ++visit_node) {
                const auto &visit = CalendarVisit(visit_node);
                if (visit.datetime().date() != schedule_day) {
                    throw util::ApplicationError("Visits span across multiple days.", util::ErrorCode::ERROR);
                }
            }
        }

        operations_research::Solver *const solver = model.solver();
        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto &carer = Carer(operations_research::RoutingModel::NodeIndex{vehicle});
            const auto &diary = problem_.diary(carer, schedule_day);

            const auto breaks = Breaks(solver, carer, diary.get());
            time_dimension->SetBreakIntervalsOfVehicle(breaks, vehicle);
        }

        // set visit start times
        for (operations_research::RoutingModel::NodeIndex visit_node{1}; visit_node < model.nodes(); ++visit_node) {
            const auto &visit = CalendarVisit(visit_node);
            const auto visit_index = model.NodeToIndex(visit_node);

            const auto exact_start = visit.datetime().time_of_day();
            auto visit_start = time_dimension->CumulVar(visit_index);
            if (HasTimeWindows()) {
                visit_start->SetRange(GetBeginWindow(exact_start), GetEndWindow(exact_start));

                const auto start_window = boost::posix_time::seconds(GetBeginWindow(exact_start));
                const auto end_window = boost::posix_time::seconds(GetEndWindow(exact_start));
                DCHECK_LT(start_window, end_window);
                DCHECK_EQ((start_window + end_window) / 2, exact_start);
            } else {
                visit_start->SetValue(exact_start.total_seconds());
            }

            model.AddToAssignment(time_dimension->SlackVar(visit_index));

            visit_index_.insert({visit, visit_node});
        }

        for (const auto &carer_pair : problem_.carers()) {
            care_continuity_metrics_.emplace_back(*this, carer_pair.first);
        }

        std::vector<operations_research::RoutingModel::NodeEvaluator2 *> care_continuity_evaluators;
        for (const auto &carer_metrics : care_continuity_metrics_) {
            care_continuity_evaluators.push_back(
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
        for (const auto &service_user : problem_.service_users()) {
            std::vector<operations_research::IntVar *> service_user_visits;

            for (const auto &visit : problem_.visits()) {
                if (visit.service_user() != service_user) {
                    continue;
                }

                const auto visit_it = visit_index_.left.find(visit);
                DCHECK(visit_it != std::end(visit_index_.left));

                service_user_visits.push_back(care_continuity_dimension->TransitVar(visit_it->second.value()));
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
                   % std::chrono::duration_cast<std::chrono::seconds>(
                end_time_distance_computation - start_time_distance_computation).count();

        VLOG(1) << "Finalizing definition of the routing model...";
        const auto start_time_model_closing = std::chrono::high_resolution_clock::now();

        model.CloseModelWithParameters(parameters_);

        const auto end_time_model_closing = std::chrono::high_resolution_clock::now();
        VLOG(1) << boost::format("Definition of the routing model finalized in %1% seconds")
                   % std::chrono::duration_cast<std::chrono::seconds>(
                end_time_model_closing - start_time_model_closing).count();
    }

    const operations_research::RoutingSearchParameters &SolverWrapper::parameters() const {
        return parameters_;
    }

    const Location &SolverWrapper::depot() const {
        return depot_;
    }

    std::vector<std::vector<std::pair<operations_research::RoutingModel::NodeIndex, rows::ScheduledVisit>>>

    SolverWrapper::GetRoutes(const rows::Solution &solution, const operations_research::RoutingModel &model) const {
        std::vector<std::vector<std::pair<operations_research::RoutingModel::NodeIndex, rows::ScheduledVisit >>>
                routes;
        for (operations_research::RoutingModel::NodeIndex vehicle{0}; vehicle < model.vehicles(); ++vehicle) {
            const auto carer = Carer(vehicle);
            std::vector<std::pair<operations_research::RoutingModel::NodeIndex, rows::ScheduledVisit >> route;
            const auto local_route = solution.GetRoute(carer);
            for (const auto &element : local_route.visits()) {
                const auto index = TryIndex(element);
                if (index.is_initialized()) {
                    route.emplace_back(index.get(), element);
                }
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
            for (operations_research::RoutingModel::NodeIndex vehicle{0}; vehicle < model.vehicles(); ++vehicle) {
                const auto carer = Carer(vehicle);
                routes.push_back(solution_to_use.GetRoute(carer));
            }

            validation_errors = validator.Validate(routes, problem, *this);
            if (VLOG_IS_ON(1)) {
                for (const auto &error_ptr : validation_errors) {
                    VLOG(1) << *error_ptr;
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
                    throw util::ApplicationError((boost::format("Error code %1% ignored") % error->error_code()).str(),
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
                    if (visit_route_it != std::end(route_it->visits())) {
                        if (visit_route_it->carer().is_initialized()) {
                            ++route_it;
                            break;
                        }
                    }
                }

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

    int64 SolverWrapper::Preference(operations_research::RoutingModel::NodeIndex to, const rows::Carer &carer) const {
        const auto &service_user = ServiceUser(to);
        return service_user.Preference(carer);
    }

    std::tuple<double, double, double> SolverWrapper::ToCartesianCoordinates(const osrm::FixedLatitude &latitude,
                                                                             const osrm::FixedLongitude &longitude) const {
        const auto latitude_to_use = (static_cast<double>(osrm::toFloating(latitude)) * M_PI) / 180.0;
        const auto longitude_to_use = (static_cast<double>(osrm::toFloating(longitude)) * M_PI) / 180.0;

        double x = cos(latitude_to_use) * cos(longitude_to_use);
        double y = cos(latitude_to_use) * sin(longitude_to_use);
        double z = sin(latitude_to_use);
        return {x, y, z};
    }

    std::pair<osrm::FixedLatitude, osrm::FixedLongitude>
    SolverWrapper::ToGeographicCoordinates(const std::tuple<double, double, double> &coordinates) const {
        const auto longitude = (atan2(std::get<1>(coordinates), std::get<0>(coordinates)) * 180.0) / M_PI;
        const auto hyperplane = sqrt(pow(std::get<0>(coordinates), 2.0) + pow(std::get<1>(coordinates), 2.0));
        const auto latitude = (atan2(std::get<2>(coordinates), hyperplane) * 180.0) / M_PI;

        return std::make_pair(osrm::toFixed(osrm::util::FloatLatitude{latitude}),
                              osrm::toFixed(osrm::util::FloatLongitude{longitude}));
    }

    std::tuple<double, double, double>
    SolverWrapper::GetAveragePoint(const std::vector<std::tuple<double, double, double>> &points) const {
        double x = 0.0, y = 0.0, z = 0.0;
        for (const auto &point : points) {
            x += std::get<0>(point);
            y += std::get<1>(point);
            z += std::get<2>(point);
        }
        const double points_count = std::max(static_cast<double>(points.size()), 1.0);
        return {x / points_count, y / points_count, z / points_count};
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
            auto carer = Carer(operations_research::RoutingModel::NodeIndex{vehicle});
            std::vector<rows::ScheduledVisit> carer_visits;

            auto order = model.Start(vehicle);
            if (!model.IsEnd(solution.Value(model.NextVar(order)))) {
                while (!model.IsEnd(order)) {
                    const auto visit_index = model.IndexToNode(order);
                    if (visit_index != DEPOT) {
                        carer_visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN,
                                                  carer,
                                                  CalendarVisit(visit_index));
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

            std::unordered_set<std::pair<osrm::util::FixedLongitude, osrm::util::FixedLatitude> > coordinates;
            for (const auto &route_pair : route_pairs) {
                if (!route_pair.second.error()) {
                    continue;
                }

                for (const auto &visit : route_pair.first.visits()) {
                    const auto &location = visit.location().get();
                    coordinates.insert(std::make_pair(location.longitude(), location.latitude()));
                }
            }

            osrm::EngineConfig config;
            config.storage_config = osrm::StorageConfig("/home/pmateusz/dev/cordia/data/scotland-latest.osrm");
            config.use_shared_memory = false;
            config.algorithm = osrm::EngineConfig::Algorithm::MLD;

            const osrm::OSRM engine{config};

            const auto get_distance = [&engine](std::pair<osrm::util::FixedLongitude, osrm::util::FixedLatitude> source,
                                                std::pair<osrm::util::FixedLongitude, osrm::util::FixedLatitude> destination) -> double {
                osrm::RouteParameters params;
                params.coordinates.emplace_back(source.first, source.second);
                params.coordinates.emplace_back(destination.first, destination.second);

                osrm::json::Object result;
                engine.Route(params, result);
                auto &routes = result.values["routes"].get<osrm::json::Array>();
                auto &route = routes.values.at(0).get<osrm::json::Object>();
                return route.values["duration"].get<osrm::json::Number>().value;
            };

            for (const auto &source: coordinates) {
                std::vector<double> distances;
                for (const auto &target: coordinates) {
                    distances.push_back(get_distance(source, target));
                }

                std::vector<std::string> text_distances;
                std::transform(std::cbegin(distances),
                               std::cend(distances),
                               std::back_inserter(text_distances),
                               [](double value) -> std::string {
                                   return std::to_string(static_cast<int>(std::ceil(value)));
                               });

                VLOG(1) << boost::format("(%1%,%2%) : %3%")
                           % source.first
                           % source.second
                           % boost::algorithm::join(text_distances, ", ");
            }

            auto vehicle = 0;
            for (const auto &route_pair : route_pairs) {
                if (static_cast<bool>(route_pair.second.error())) {
                    VLOG(1) << boost::format("Route %1%: %2%")
                               % vehicle
                               % *route_pair.second.error();

                    VLOG(1) << "Expected path: ";
                    for (const auto &visit : route_pair.first.visits()) {
                        const auto &location = visit.location().get();
                        VLOG(1) << boost::format("(%1%,%2%) [%3%, %4%] %5% %6%")
                                   % location.latitude()
                                   % location.longitude()
                                   % boost::posix_time::seconds(GetBeginWindow(visit.datetime().time_of_day()))
                                   % boost::posix_time::seconds(GetEndWindow(visit.datetime().time_of_day()))
                                   % visit.duration()
                                   % visit.service_user();
                    }

                    VLOG(1) << "Solved path: ";
                    auto order = model.Start(vehicle);
                    if (!model.IsEnd(solution.Value(model.NextVar(order)))) {
                        while (!model.IsEnd(order)) {
                            const auto visit_index = model.IndexToNode(order);
                            if (visit_index != DEPOT) {
                                const auto time_var = time_dimension->CumulVar(order);
                                VLOG(1) << boost::format("[%1%, %2%]")
                                           % boost::posix_time::seconds(solution.Min(time_var))
                                           % boost::posix_time::seconds(solution.Max(time_var));
                            }

                            order = solution.Value(model.NextVar(order));
                        }
                    }
                }

                ++vehicle;
            }

            for (const auto &break_interval : solution.IntervalVarContainer().elements()) {
                const auto start = boost::posix_time::seconds(break_interval.StartValue());
                const auto finish = start + boost::posix_time::seconds(break_interval.DurationValue());

                if (break_interval.PerformedValue() == 1) {
                    VLOG(1) << boost::format("%1% [%2%,%3%] %4%")
                               % break_interval.Var()->name()
                               % start
                               % finish
                               % break_interval.DebugString();
                } else {
                    VLOG(1) << break_interval.Var()->name() << " unperformed";
                }
            }
        }

        VLOG(1) << model.DebugOutputAssignment(solution, "");

        DCHECK_EQ(total_errors, 0);

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
    SolverWrapper::CareContinuityDimVar(const rows::ExtendedServiceUser &service_user) const {
        const auto service_user_it = care_continuity_.find(service_user);
        DCHECK(service_user_it != std::cend(care_continuity_));
        return service_user_it->second;
    }

    std::size_t SolverWrapper::PartialVisitOperations::operator()(const rows::CalendarVisit &object) const noexcept {
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
        const auto visit_it_end = std::cend(solver.visit_index_.left);
        for (auto visit_it = std::cbegin(solver.visit_index_.left); visit_it != visit_it_end; ++visit_it) {
            const auto &visit_index = visit_it->second;
            const auto &service_user = solver.ServiceUser(visit_index);
            if (service_user.IsPreferred(carer)) {
                values_.insert(std::make_pair(visit_index, service_user.Preference(carer)));
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
