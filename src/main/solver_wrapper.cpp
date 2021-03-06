#include <algorithm>
#include <numeric>
#include <vector>
#include <chrono>
#include <tuple>
#include <cmath>

#include <glog/logging.h>

#include <boost/date_time.hpp>
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/variance.hpp>
#include <boost/format.hpp>

#include <ortools/sat/integer_expr.h>
#include <ortools/constraint_solver/routing_parameters.h>
#include <util/aplication_error.h>

#include <osrm/json_container.hpp>
#include <osrm/engine/api/route_parameters.hpp>


#include "calendar_visit.h"
#include "carer.h"
#include "scheduled_visit.h"
#include "solution.h"
#include "solver_wrapper.h"
#include "progress_printer_monitor.h"
#include "declined_visit_evaluator.h"

// TODO: add support for mobile workers
// TODO: add information about partner
// TODO: add information about back to back carers
// TODO: modify cost function to differentiate between each of employee category

namespace rows {

    const int64 SolverWrapper::MAX_CARERS_SINGLE_VISITS = 2;
    const int64 SolverWrapper::MAX_CARERS_MULTIPLE_VISITS = 4;

    const std::string SolverWrapper::TIME_DIMENSION{"Time"};

    SolverWrapper::SolverWrapper(const ProblemData &problem_data,
                                 const operations_research::RoutingSearchParameters &search_parameters)
            : SolverWrapper(problem_data,
                            search_parameters,
                            boost::posix_time::minutes(120),
                            boost::posix_time::minutes(120),
                            boost::posix_time::not_a_date_time) {}

    SolverWrapper::SolverWrapper(const ProblemData &problem_data,
                                 const operations_research::RoutingSearchParameters &search_parameters,
                                 boost::posix_time::time_duration visit_time_window,
                                 boost::posix_time::time_duration break_time_window,
                                 boost::posix_time::time_duration begin_end_work_day_adjustment)
            : depot_service_user_(),
              visit_time_window_(visit_time_window),
              break_time_window_(break_time_window),
              begin_end_work_day_adjustment_(begin_end_work_day_adjustment),
            // time when carer is out of office is considered as a break
              out_office_hours_breaks_enabled_(true),
              parameters_(search_parameters),
              service_users_(),
              problem_data_{problem_data},
              index_manager_{nodes(), vehicles(), rows::RealProblemData::DEPOT},
              failed_index_repository_{std::make_shared<FailedIndexRepository>()} {
        for (const auto &service_user : problem_data_.problem().service_users()) {
            const auto visit_count = std::count_if(std::begin(problem_data_.problem().visits()),
                                                   std::end(problem_data_.problem().visits()),
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

    boost::optional<rows::Diary> FindDiaryOrNone(const std::vector<rows::Diary> &diaries, boost::gregorian::date date) {
        const auto find_date_it = std::find_if(std::begin(diaries),
                                               std::end(diaries),
                                               [&date](const rows::Diary &diary) -> bool {
                                                   return diary.date() == date;
                                               });
        if (find_date_it != std::end(diaries)) {
            return boost::make_optional(*find_date_it);
        }
        return boost::none;
    }

    const rows::Carer &SolverWrapper::Carer(int vehicle) const {
        return problem().carers().at(static_cast<std::size_t>(vehicle)).first;
    }

    const rows::SolverWrapper::LocalServiceUser &SolverWrapper::User(const rows::ServiceUser &user) const {
        const auto find_it = service_users_.find(user);
        DCHECK(find_it != std::end(service_users_));
        return find_it->second;
    }

    std::vector<rows::Event> SolverWrapper::GetEffectiveBreaks(const rows::Diary &diary) const {
        const boost::posix_time::time_period time_horizon{StartHorizon(), EndHorizon()};
        const auto original_breaks = diary.Breaks(time_horizon);

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
        const auto start_time = event.begin() - StartHorizon();
        CHECK(!start_time.is_negative());

        const auto raw_duration = event.duration().total_seconds();
        const auto begin_break_window = this->GetBeginBreakWindow(start_time);
        const auto end_break_window = this->GetEndBreakWindow(start_time);

        CHECK_LE(begin_break_window, end_break_window);
        CHECK_GT(raw_duration, 0);
        // TODO: consider more strict tests to ensure that end break window is within the time horizon

        return solver->MakeIntervalVar(
                begin_break_window, end_break_window,
                raw_duration, raw_duration,
                begin_break_window + raw_duration,
                end_break_window + raw_duration,
                /*optional*/false,
                label);
    }

    operations_research::IntervalVar *SolverWrapper::CreateFixedInterval(
            operations_research::Solver *solver,
            const rows::Event &event,
            std::string label) const {
        const auto start_time = event.begin() - StartHorizon();
        CHECK(!start_time.is_negative());
        CHECK_GE(event.duration().total_seconds(), 0);

        return solver->MakeFixedInterval(start_time.total_seconds(),
                                         event.duration().total_seconds(),
                                         label);
    }

    bool SolverWrapper::AddFixedIntervalIfNonZero(operations_research::Solver *solver,
                                                  const rows::Event &event,
                                                  std::string label,
                                                  std::vector<operations_research::IntervalVar *> &intervals) const {
        if (event.duration().total_seconds() >= 0) {
            intervals.push_back(CreateFixedInterval(solver, event, label));
            return true;
        }
        return false;
    }

    bool SolverWrapper::AddBreakIntervalVarIfNonZero(operations_research::Solver *solver,
                                                     const rows::Event &event,
                                                     std::string label,
                                                     std::vector<operations_research::IntervalVar *> &intervals) const {
        if (event.duration().total_seconds() > 0) {
            intervals.push_back(CreateBreakInterval(solver, event, label));
            return true;
        }
        return false;
    }

    std::vector<operations_research::IntervalVar *> SolverWrapper::CreateBreakIntervals(
            operations_research::Solver *const solver,
            const rows::Carer &carer,
            const rows::Diary &diary) const {
        // intervals must not have zero duration, otherwise they may result in violation of the all distinct constraint

        std::vector<operations_research::IntervalVar *> break_intervals;

        const auto break_periods = GetEffectiveBreaks(diary);
        if (break_periods.empty()) {
            return break_intervals;
        }

        for (const auto &break_period : break_periods) {
            CHECK_LE(break_period.begin(), break_period.end());
        }

        if (out_office_hours_breaks_enabled_) {
            const auto &break_before_work = break_periods.front();
            CHECK_EQ(break_before_work.begin().time_of_day().total_seconds(), 0);
            AddFixedIntervalIfNonZero(solver,
                                      break_before_work,
                                      GetBreakLabel(carer, BreakType::BEFORE_WORKDAY),
                                      break_intervals);

            const auto last_break = break_periods.size() - 1;
            for (auto break_index = 1; break_index < last_break; ++break_index) {
                AddBreakIntervalVarIfNonZero(solver,
                                             break_periods[break_index],
                                             SolverWrapper::GetBreakLabel(carer, BreakType::BREAK),
                                             break_intervals);
            }

            const auto &break_after_work = break_periods.back();
            CHECK_EQ(break_after_work.end(), EndHorizon());
            AddFixedIntervalIfNonZero(solver,
                                      break_after_work,
                                      GetBreakLabel(carer, BreakType::AFTER_WORKDAY),
                                      break_intervals);
        } else {
            for (const auto &break_period : break_periods) {
                AddBreakIntervalVarIfNonZero(solver,
                                             break_period,
                                             SolverWrapper::GetBreakLabel(carer, BreakType::BREAK),
                                             break_intervals);
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

    const operations_research::RoutingSearchParameters &SolverWrapper::parameters() const {
        return parameters_;
    }

    boost::posix_time::time_duration abs_time_distance(const boost::posix_time::ptime &left,
                                                       const boost::posix_time::ptime &right) {
        auto time_distance = left - right;
        if (time_distance.is_negative()) {
            return -time_distance;
        }
        return time_distance;
    }

    std::vector<std::vector<int64>>
    SolverWrapper::GetRoutes(const rows::Solution &solution, const operations_research::RoutingModel &model) const {
        std::vector<std::vector<int64>> routes;
        std::unordered_set<operations_research::RoutingNodeIndex> used_nodes;


        // for each scheduled visit find its calendar visit
        std::unordered_map<rows::CalendarVisit, boost::optional<rows::CalendarVisit>> matching;
        CHECK_EQ(index_manager_.num_nodes(), problem_data_.nodes());

        // counting from 1 to handle depot
        for (operations_research::RoutingNodeIndex node_index{1}; node_index < index_manager_.num_nodes(); ++node_index) {
            matching.emplace(problem_data_.NodeToVisit(node_index), boost::none);
        }

        for (const auto &visit : solution.visits()) {
            for (auto &item : matching) {
                if (IsNear(item.first, visit.calendar_visit().get())) {
                    if (!item.second || item.first.id() == visit.calendar_visit()->id()) {
                        item.second = visit.calendar_visit().get();
                    } else if (item.first.id() != item.second->id()) {
                        const auto current_distance = abs_time_distance(item.first.datetime(), item.second->datetime());
                        const auto other_distance = abs_time_distance(item.first.datetime(), visit.datetime());
                        if (other_distance < current_distance) {
                            item.second = visit.calendar_visit().get();
                        }
                    }
                }
            }
        }

//        auto not_matched_visits = 0;
//        for (const auto &item : matching) {
//            if (!item.second) {
//                LOG(WARNING) << "Visit not matched " << item.first;
//                ++not_matched_visits;
//            }
//        }

        std::unordered_map<rows::CalendarVisit, rows::CalendarVisit> reverse_matching;
        for (const auto &item : matching) {
            if (item.second) {
                reverse_matching.emplace(item.second.get(), item.first);
            }
        }

        std::unordered_set<rows::Carer> used_carers;
//        CHECK_EQ(model.vehicles(), solution.Carers().size());
        for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto carer = Carer(vehicle);
            used_carers.insert(carer);

            std::vector<int64> route;
            const auto local_route = solution.GetRoute(carer);
//            if (vehicle == 61) {
//                for (const auto &event : GetEffectiveBreaks(*problem().diary(carer, GetScheduleDate()))) {
//                    LOG(INFO) << event;
//                }
//
//                for (const auto &visit : local_route.visits()) {
//                    LOG(INFO) << visit;
//                }
//            }

            for (const auto &visit : local_route.visits()) {
                const auto find_it = reverse_matching.find(visit.calendar_visit().get());
                if (find_it == std::end(reverse_matching)) {
                    LOG(INFO) << "Visit not found: " << visit.calendar_visit().get();
                    for (const auto &visit_item: matching) {
                        LOG(INFO) << visit_item.first;
                    }

                    continue;
                }

                const auto &visit_nodes = GetNodes(find_it->second);
                CHECK(!visit_nodes.empty());

                auto inserted = false;
                for (const auto &node : visit_nodes) {
                    if (used_nodes.find(node) != std::end(used_nodes)) {
                        continue;
                    }

                    route.push_back(index_manager_.NodeToIndex(node));
                    inserted = used_nodes.insert(node).second;
                    break;
                }

                CHECK(inserted);
            }

            CHECK_EQ(route.size(), local_route.visits().size());
            routes.emplace_back(std::move(route));
        }

//        CHECK_EQ(used_carers.size(), solution.Carers().size());
//        CHECK_EQ(routes.size(), solution.Carers().size());

        for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto carer = Carer(vehicle);

            const auto local_route = solution.GetRoute(carer);
            CHECK_EQ(routes.at(vehicle).size(), local_route.visits().size());
        }

        const auto &solution_visits = solution.visits();
        for (operations_research::RoutingNodeIndex node_index{1}; node_index < index_manager_.num_nodes(); ++node_index) {
            const auto &visit = problem_data_.NodeToVisit(node_index);
            const auto &visit_nodes = problem_data_.GetNodes(visit);

            const auto find_it = matching.find(visit);
            if (find_it == std::end(matching) || !find_it->second) {
                continue;
            }

            auto solution_count = 0;
            for (const auto &carer : solution.Carers()) {
                const auto &route = solution.GetRoute(carer);
                for (const auto &visit_candidate :route.visits()) {
                    if (visit_candidate.calendar_visit()->id() == visit.id()) {
                        ++solution_count;
                    }
                }
            }

            auto routes_count = 0;
            for (const auto &route : routes) {
                for (const auto index : route) {
                    const auto visit_node = index_manager_.IndexToNode(index);
                    const auto find_it = std::find(std::cbegin(visit_nodes), std::cend(visit_nodes), visit_node);
                    if (find_it != std::cend(visit_nodes)) {
                        ++routes_count;
                    }
                }
            }

            CHECK_EQ(routes_count, solution_count) << visit;
        }

        auto total_nodes_visited = 0;
        for (const auto &route : routes) {
            total_nodes_visited += route.size();
        }

        auto total_nodes_solution = 0;
        for (const auto &carer : solution.Carers()) {
            const auto &solution_route = solution.GetRoute(carer);
            total_nodes_solution += solution_route.visits().size();
        }
        CHECK_EQ(total_nodes_visited, total_nodes_solution);

//        LOG(INFO) << Carer(61) << " " << routes.at(61).at(0);
//        const auto &diary_opt = problem().diary(Carer(61), GetScheduleDate());
//        if (diary_opt) {
//            for (const auto &event : diary_opt->events()) {
//                LOG(INFO) << event;
//            }
//        }
//
//        LOG(INFO) << NodeToVisit(index_manager_.IndexToNode(615));

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

            validation_errors = validator.ValidateAll(routes, problem_data_.problem(), *this);
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
            const auto initial_size = std::count_if(std::begin(solution.visits()),
                                                    std::end(solution.visits()),
                                                    is_assigned);
            const auto reduced_size = std::count_if(std::begin(solution_to_use.visits()),
                                                    std::end(solution_to_use.visits()),
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

        return rows::Solution(std::move(visits_to_use), std::vector<Break>());
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
        return std::max(static_cast<int64>((value - window_size).total_seconds()), static_cast<int64>(0));
    }

    int64 SolverWrapper::GetEndWindow(boost::posix_time::time_duration value,
                                      boost::posix_time::time_duration window_size) const {
        return std::min(static_cast<int64>((value + window_size).total_seconds()),
                        static_cast<int64>((problem_data_.EndHorizon() - problem_data_.StartHorizon()).total_seconds()));
    }

    const Problem &SolverWrapper::problem() const {
        return problem_data_.problem();
    }

    std::string SolverWrapper::GetDescription(const operations_research::RoutingModel &model, const operations_research::Assignment &solution) const {
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
                    const auto visit_index = index_manager_.IndexToNode(order);
                    DCHECK_NE(visit_index, RealProblemData::DEPOT);
                    carer_visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN, carer, NodeToVisit(visit_index));
                }

                order = solution.Value(model.NextVar(order));
            }

            rows::Route route{carer, carer_visits};
            VLOG(2) << "Route: " << vehicle;
            RouteValidatorBase::ValidationResult validation_result = route_validator.ValidateFull(vehicle, solution, model, *this);
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
        stats.CarerUtility.TotalMean = (static_cast<double>(total_work_time.total_seconds()) / total_available_time.total_seconds());

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
        for (const auto &carer_diary_pairs : problem().carers()) {
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
        return problem_data_.vehicles();
    }

    int SolverWrapper::nodes() const {
        return problem_data_.nodes();
    }

    bool SolverWrapper::Contains(const CalendarVisit &visit) const {
        return problem_data_.Contains(visit);
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
        return std::min(static_cast<int64>((finish_time + begin_end_work_day_adjustment_).total_seconds()), RealProblemData::SECONDS_IN_DIMENSION);
    }

    boost::posix_time::time_duration SolverWrapper::GetAdjustment() const {
        if (begin_end_work_day_adjustment_.is_special()) {
            return {};
        }
        return begin_end_work_day_adjustment_;
    }

    boost::gregorian::date SolverWrapper::GetScheduleDate() const {
        return NodeToVisit(operations_research::RoutingNodeIndex{1}).datetime().date();
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

    std::pair<operations_research::RoutingNodeIndex, operations_research::RoutingNodeIndex>
    SolverWrapper::GetNodePair(const rows::CalendarVisit &visit) const {
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

    bool SolverWrapper::out_office_hours_breaks_enabled() const {
        return out_office_hours_breaks_enabled_;
    }

    const boost::posix_time::ptime SolverWrapper::StartHorizon() const {
        return problem_data_.StartHorizon();
    }

    const boost::posix_time::ptime SolverWrapper::EndHorizon() const {
        return problem_data_.EndHorizon();
    }

    SolverWrapper::LocalServiceUser::LocalServiceUser()
            : LocalServiceUser(ExtendedServiceUser(), 1) {}


    SolverWrapper::LocalServiceUser::LocalServiceUser(const rows::ExtendedServiceUser &service_user, int64 visit_count)
            : service_user_(service_user),
              visit_count_(visit_count) {}

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

    int64 SolverWrapper::Distance(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const {
        return problem_data_.Distance(from, to);
    }

    int64 SolverWrapper::ServiceTime(operations_research::RoutingNodeIndex node) {
        return problem_data_.ServiceTime(node);
    }

    int64 SolverWrapper::ServicePlusTravelTime(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) {
        return problem_data_.ServicePlusTravelTime(from, to);
    }

    bool SolverWrapper::IsNear(const rows::CalendarVisit &left, const rows::CalendarVisit &right) const {
        const auto is_within_windows =
                GetBeginVisitWindow(left.datetime().time_of_day()) <= right.datetime().time_of_day().total_seconds()
                && right.datetime().time_of_day().total_seconds() <= GetEndVisitWindow(left.datetime().time_of_day());

        if (left.id() == right.id()) {
            CHECK_EQ(left.duration(), right.duration());
            CHECK_EQ(left.service_user(), right.service_user());
            CHECK(is_within_windows);
            return true;
        }

        return left.duration() == right.duration() && left.service_user() == right.service_user() && is_within_windows;
    }

    void SolverWrapper::AddSkillHandling(operations_research::RoutingModel &model) {
        for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
            const auto &visit = problem_data_.NodeToVisit(visit_node);

            std::vector<int64> visit_indices;
            for (const auto local_visit_node : problem_data_.GetNodes(visit)) {
                visit_indices.push_back(index_manager_.NodeToIndex(local_visit_node));
            }

            std::vector<int64> allowed_vehicles{index_manager_.kUnassigned};
            for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
                const auto &carer = Carer(vehicle);
                if (carer.has_skills(visit.tasks())) {
                    allowed_vehicles.push_back(vehicle);
                }
            }

            for (auto visit_index : visit_indices) {
                model.solver()->AddConstraint(model.solver()->MakeMemberCt(model.VehicleVar(visit_index), allowed_vehicles));
            }
        }
    }

    void SolverWrapper::AddContinuityOfCare(operations_research::RoutingModel &model) {
        for (const auto &service_user : service_users_) {
            std::vector<int64> user_visit_indices;
            bool is_multiple_carer_service_user = false;

            std::vector<int64> visit_indices;
            for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
                const auto &visit = problem_data_.NodeToVisit(visit_node);
                const auto &visit_nodes = problem_data_.GetNodes(visit);

                if (visit.service_user() != service_user.first) {
                    continue;
                }

                for (const auto local_visit_node : visit_nodes) {
                    const auto visit_index = index_manager_.NodeToIndex(local_visit_node);
                    user_visit_indices.push_back(visit_index);
                }

                if (visit_nodes.size() > 1) {
                    is_multiple_carer_service_user = true;
                }
            }

            std::vector<operations_research::IntVar *> is_visited_by_vehicle;
            model.solver()->MakeBoolVarArray(vehicles(), &is_visited_by_vehicle);

            for (auto visit_index : visit_indices) {
                model.solver()->MakeElementEquality(is_visited_by_vehicle, model.VehicleVar(visit_index), 1);
            }

            auto continuity_care_cardinality = MAX_CARERS_SINGLE_VISITS;
            if (is_multiple_carer_service_user) {
                continuity_care_cardinality = MAX_CARERS_MULTIPLE_VISITS;
            }

            model.solver()->AddConstraint(
                    model.solver()->MakeLessOrEqual(model.solver()->MakeSum(is_visited_by_vehicle), continuity_care_cardinality));
        }
    }

    void SolverWrapper::AddTravelTime(operations_research::RoutingModel &model) {
        static const auto START_FROM_ZERO_TIME = false;

        const auto transit_callback_handle = model.RegisterTransitCallback([this](int64 from_index, int64 to_index) -> int64 {
            return this->problem_data_.Distance(this->index_manager_.IndexToNode(from_index), this->index_manager_.IndexToNode(to_index));
        });
        model.SetArcCostEvaluatorOfAllVehicles(transit_callback_handle);

        const auto service_time_callback_handle = model.RegisterTransitCallback([this](int64 from_index, int64 to_index) -> int64 {
            return this->problem_data_.ServicePlusTravelTime(this->index_manager_.IndexToNode(from_index),
                                                             this->index_manager_.IndexToNode(to_index));
        });

        const auto seconds_in_horizon = (problem_data_.EndHorizon() - problem_data_.StartHorizon()).total_seconds();
        model.AddDimension(service_time_callback_handle, seconds_in_horizon, seconds_in_horizon, START_FROM_ZERO_TIME, TIME_DIMENSION);
    }

    void SolverWrapper::AddVisitsHandling(operations_research::RoutingModel &model) {
        operations_research::RoutingDimension *time_dimension = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

        time_dimension->CumulVar(index_manager_.NodeToIndex(RealProblemData::DEPOT))->SetRange(0, RealProblemData::SECONDS_IN_DIMENSION);

        // visit that needs multiple carers is referenced by multiple nodes
        // all such nodes must be either performed or unperformed
        auto total_multiple_carer_visits = 0;
        for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
            const auto &visit = problem_data_.NodeToVisit(visit_node);

            const auto &visit_time_windows = visit.time_windows();
            const auto visit_start_begin = visit_time_windows.begin() - problem_data_.StartHorizon();
            const auto visit_start_end = visit_time_windows.end() - problem_data_.StartHorizon();

            CHECK(!visit_start_begin.is_negative()) << visit.id();
            CHECK(!visit_start_end.is_negative()) << visit.id();
            CHECK_LE(visit_start_begin, visit_start_end);

            std::vector<int64> visit_indices;
            for (const auto &local_visit_node : problem_data_.GetNodes(visit_node)) {
                const auto visit_index = index_manager_.NodeToIndex(local_visit_node);
                visit_indices.push_back(visit_index);

                if (HasTimeWindows()) {
                    const auto start_window = GetBeginVisitWindow(visit_start_begin);
                    const auto end_window = GetEndVisitWindow(visit_start_end);

                    time_dimension
                            ->CumulVar(visit_index)
                            ->SetRange(start_window, end_window);

                    DCHECK_LT(start_window, end_window) << visit.id();
                    DCHECK_LE(start_window, visit_start_begin.total_seconds()) << visit.id();
                    DCHECK_LE(visit_start_begin.total_seconds(), end_window) << visit.id();
                } else if (visit_start_begin < visit_start_end) {
                    time_dimension
                            ->CumulVar(visit_index)
                            ->SetRange(visit_start_begin.total_seconds(), visit_start_end.total_seconds());
                } else {
                    time_dimension->CumulVar(visit_index)->SetValue(visit_start_begin.total_seconds());
                }

                model.AddToAssignment(time_dimension->CumulVar(visit_index));
                model.AddToAssignment(time_dimension->SlackVar(visit_index));
            }

            const auto visit_indices_size = visit_indices.size();
            if (visit_indices_size > 1) {
                CHECK_EQ(visit_indices_size, 2);

                auto first_visit_to_use = visit_indices[0];
                auto second_visit_to_use = visit_indices[1];
                if (first_visit_to_use > second_visit_to_use) {
                    std::swap(first_visit_to_use, second_visit_to_use);
                }

                model.solver()->AddConstraint(model.solver()->MakeLessOrEqual(time_dimension->CumulVar(first_visit_to_use),
                                                                              time_dimension->CumulVar(second_visit_to_use)));
                model.solver()->AddConstraint(model.solver()->MakeLessOrEqual(time_dimension->CumulVar(second_visit_to_use),
                                                                              time_dimension->CumulVar(first_visit_to_use)));
                model.solver()->AddConstraint(model.solver()->MakeLessOrEqual(model.ActiveVar(first_visit_to_use),
                                                                              model.ActiveVar(second_visit_to_use)));
                model.solver()->AddConstraint(model.solver()->MakeLessOrEqual(model.ActiveVar(second_visit_to_use),
                                                                              model.ActiveVar(first_visit_to_use)));

                const auto second_vehicle_var_to_use = model.solver()->MakeMax(model.VehicleVar(second_visit_to_use),
                                                                               model.solver()->MakeIntConst(0));
                model.solver()->AddConstraint(model.solver()->MakeLess(model.VehicleVar(first_visit_to_use), second_vehicle_var_to_use));

                ++total_multiple_carer_visits;
            }
        }
    }

    void SolverWrapper::AddCarerHandling(operations_research::RoutingModel &model) {
        operations_research::RoutingDimension *time_dimension = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

        // could be interesting to use the Google constraint for breaks
        // initial results show violation of some breaks
        std::vector<int64> service_times(model.Size());
        for (operations_research::RoutingNodeIndex node{0}; node < model.Size(); ++node) {
            if (node >= model.nodes() || node == 0) {
                service_times.at(node.value()) = 0;
            } else {
                service_times.at(node.value()) = problem_data_.ServiceTime(node);
            }
        }

        const auto schedule_day = GetScheduleDate();
        auto solver_ptr = model.solver();
        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto &carer = Carer(vehicle);
            const auto &diary_opt = problem().diary(carer, schedule_day);

            int64 begin_time = 0;
            int64 end_time = 0;
            if (diary_opt.is_initialized()) {
                const auto &diary = diary_opt.get();

                const auto begin_time_duration = (diary.begin_date_time() - StartHorizon());
                const auto end_time_duration = (diary.end_date_time() - StartHorizon());

                CHECK(!begin_time_duration.is_negative()) << carer.sap_number();
                CHECK(!end_time_duration.is_negative()) << carer.sap_number();

                begin_time = GetAdjustedWorkdayStart(begin_time_duration);
                end_time = GetAdjustedWorkdayFinish(end_time_duration);

                CHECK_GE(begin_time, 0) << carer.sap_number();
                CHECK_LE(begin_time, end_time) << carer.sap_number();

                const auto breaks = CreateBreakIntervals(solver_ptr, carer, diary);
                if (!breaks.empty()) {
                    for (const auto &break_item : breaks) {
                        model.AddIntervalToAssignment(break_item);
                    }

                    VLOG(1) << "Carer:" << carer.sap_number() << " Vehicle:" << vehicle;
                    for (const auto &break_item : breaks) {
                        VLOG(1) << "[" << boost::posix_time::seconds(break_item->StartMin())
                                << ", " << boost::posix_time::seconds(break_item->StartMax())
                                << "] [" << boost::posix_time::seconds(break_item->EndMin())
                                << ", " << boost::posix_time::seconds(break_item->EndMax()) << "]";
                    }

                    time_dimension->SetBreakIntervalsOfVehicle(breaks, vehicle, service_times);
                }
            }

            time_dimension->CumulVar(model.Start(vehicle))->SetRange(begin_time, end_time);
            time_dimension->CumulVar(model.End(vehicle))->SetRange(begin_time, end_time);
        }
//    solver_ptr->AddConstraint(solver_ptr->RevAlloc(new operations_research::GlobalVehicleBreaksConstraint(time_dimension)));
    }

    void SolverWrapper::AddDroppedVisitsHandling(operations_research::RoutingModel &model) {
        const auto dropped_visits_penalty = GetDroppedVisitPenalty();
        AddDroppedVisitsHandling(model, dropped_visits_penalty);
    }

    void SolverWrapper::AddDroppedVisitsHandling(operations_research::RoutingModel &model, int64 penalty) {
        for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
            std::vector<int64> visit_indices = index_manager_.NodesToIndices(problem_data_.GetNodes(visit_node));
            model.AddDisjunction(visit_indices, penalty, static_cast<int64>(visit_indices.size()));
        }
    }

    void SolverWrapper::LimitDroppedVisits(operations_research::RoutingModel &model, int64 max_dropped_visits_threshold) {
        DeclinedVisitEvaluator evaluator{problem_data_, index_manager_};

//        std::vector<operations_research::IntVar *> all_visits;
//        for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
//            all_visits.push_back(model.VehicleVar(index_manager_.NodeToIndex(visit_node)));
//        }
//        model.solver()->AddConstraint(model.solver()->MakeAtMost(all_visits, -1, max_dropped_visits));

        std::vector<operations_research::IntVar *> weighted_sum;
        for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
            const auto index = index_manager_.NodeToIndex(visit_node);
            weighted_sum.push_back(model.solver()->MakeProd(model.ActiveVar(index), evaluator.Weight(index))->Var());
        }
        model.solver()->AddConstraint(model.solver()->MakeGreaterOrEqual(model.solver()->MakeSum(weighted_sum), max_dropped_visits_threshold));
    }

    const std::vector<operations_research::RoutingNodeIndex> &SolverWrapper::GetNodes(const ScheduledVisit &visit) const {
        return GetNodes(visit.calendar_visit().get());
    }

    const std::vector<operations_research::RoutingNodeIndex> &SolverWrapper::GetNodes(const CalendarVisit &visit) const {
        return problem_data_.GetNodes(visit);
    }

    const CalendarVisit &SolverWrapper::NodeToVisit(const operations_research::RoutingNodeIndex &node) const {
        return problem_data_.NodeToVisit(node);
    }

    int64 SolverWrapper::GetDroppedVisitPenalty() {
        return problem_data_.GetDroppedVisitPenalty();
    }

    void SolverWrapper::ConfigureModel(operations_research::RoutingModel &model,
                                       const std::shared_ptr<Printer> &printer,
                                       std::shared_ptr<const std::atomic<bool> > cancel_token,
                                       double cost_normalization_factor) {
        if (model.nodes() == 0) {
            throw util::ApplicationError("Model contains no visits.", util::ErrorCode::ERROR);
        }

        const auto schedule_day = GetScheduleDate();
        if (model.nodes() > 1) {
            for (operations_research::RoutingNodeIndex visit_node{2}; visit_node < model.nodes(); ++visit_node) {
                const auto &visit = NodeToVisit(visit_node);
                if (visit.datetime().date() != schedule_day) {
                    throw util::ApplicationError("Visits span across multiple days.", util::ErrorCode::ERROR);
                }
            }
        }

//        for (auto index = 0; index < index_manager_.num_indices(); ++index) {
//            const auto active_var = model.ActiveVar(index);
//            if (active_var != nullptr) {
//                model.AddToAssignment(model.ActiveVar(index));
//            }
//
//            const auto vehicle_var = model.VehicleVar(index);
//            if (vehicle_var != nullptr) {
//                model.AddToAssignment(model.VehicleVar(index));
//            }
//
//            const auto next_var = model.NextVar(index);
//            CHECK(next_var != nullptr);
//            model.AddToAssignment(model.NextVar(index));
//        }
    }
}
