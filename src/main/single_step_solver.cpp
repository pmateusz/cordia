#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include <chrono>
#include <utility>

#include "util/aplication_error.h"

#include "single_step_solver.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"

namespace rows {

    const int64 SingleStepSolver::CARE_CONTINUITY_MAX = 10000;

    const std::string SingleStepSolver::CARE_CONTINUITY_DIMENSION{"CareContinuity"};

    SingleStepSolver::SingleStepSolver(const rows::Problem &problem,
                                       osrm::EngineConfig &config,
                                       const operations_research::RoutingSearchParameters &search_parameters,
                                       boost::posix_time::time_duration visit_time_window,
                                       boost::posix_time::time_duration break_time_window,
                                       boost::posix_time::time_duration begin_end_work_day_adjustment,
                                       boost::posix_time::time_duration no_progress_time_limit)
            : SolverWrapper(problem,
                            config,
                            search_parameters,
                            std::move(visit_time_window),
                            std::move(break_time_window),
                            std::move(begin_end_work_day_adjustment)),
              no_progress_time_limit_(no_progress_time_limit),
              variable_store_{nullptr},
              care_continuity_enabled_(false),
              care_continuity_(),
              care_continuity_metrics_() {

        for (const auto &service_user : problem_.service_users()) {
            care_continuity_.insert(std::make_pair(service_user, nullptr));
        }
    }

    SingleStepSolver::SingleStepSolver(const rows::Problem &problem,
                                       osrm::EngineConfig &config,
                                       const operations_research::RoutingSearchParameters &search_parameters)
            : SingleStepSolver(problem,
                               config,
                               search_parameters,
                               boost::posix_time::minutes(120),
                               boost::posix_time::minutes(0),
                               boost::posix_time::minutes(0),
                               boost::posix_time::not_a_date_time) {}

    operations_research::IntVar const *SingleStepSolver::CareContinuityVar(
            const rows::ExtendedServiceUser &service_user) const {
        const auto service_user_it = care_continuity_.find(service_user);
        DCHECK(service_user_it != std::end(care_continuity_));
        return service_user_it->second;
    }

    void SingleStepSolver::ConfigureModel(operations_research::RoutingModel &model,
                                          const std::shared_ptr<Printer> &printer,
                                          std::shared_ptr<const std::atomic<bool> > cancel_token) {
        static const auto START_FROM_ZERO_SERVICE_SATISFACTION = true;
        static const auto START_FROM_ZERO_TIME = false;

        OnConfigureModel(model);

        variable_store_ = std::make_shared<rows::RoutingVariablesStore>(model.nodes(), model.vehicles());

        model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(this, &rows::SolverWrapper::Distance));
        model.AddDimension(NewPermanentCallback(this, &rows::SolverWrapper::ServicePlusTravelTime),
                           SECONDS_IN_DIMENSION,
                           SECONDS_IN_DIMENSION,
                           START_FROM_ZERO_TIME,
                           TIME_DIMENSION);

        operations_research::RoutingDimension *time_dimension
                = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

        operations_research::Solver *const solver = model.solver();
        time_dimension->CumulVar(model.NodeToIndex(DEPOT))->SetRange(0, SECONDS_IN_DIMENSION);

        // visit that needs multiple carers is referenced by multiple nodes
        // all such nodes must be either performed or unperformed
        auto total_multiple_carer_visits = 0;
        for (const auto &visit_index_pair : visit_index_) {
            const auto visit_start = visit_index_pair.first.datetime() - StartHorizon();
            DCHECK(!visit_start.is_negative()) << visit_index_pair.first.id();

            std::vector<int64> visit_indices;
            for (const auto &visit_node : visit_index_pair.second) {
                const auto visit_index = model.NodeToIndex(visit_node);
                visit_indices.push_back(visit_index);

                if (HasTimeWindows()) {
                    const auto start_window = GetBeginVisitWindow(visit_start);
                    const auto end_window = GetEndVisitWindow(visit_start);

                    time_dimension
                            ->CumulVar(visit_index)
                            ->SetRange(start_window, end_window);

                    DCHECK_LT(start_window, end_window) << visit_index_pair.first.id();
                    DCHECK_LE(start_window, visit_start.total_seconds()) << visit_index_pair.first.id();
                    DCHECK_LE(visit_start.total_seconds(), end_window) << visit_index_pair.first.id();
                } else {
                    time_dimension->CumulVar(visit_index)->SetValue(visit_start.total_seconds());
                }
                model.AddToAssignment(time_dimension->CumulVar(visit_index));
                model.AddToAssignment(time_dimension->SlackVar(visit_index));

                variable_store_->SetTimeVar(visit_index, time_dimension->CumulVar(visit_index));
                variable_store_->SetTimeSlackVar(visit_index, time_dimension->SlackVar(visit_index));
            }

            const auto visit_indices_size = visit_indices.size();
            if (visit_indices_size > 1) {
                CHECK_EQ(visit_indices_size, 2);

                auto first_visit_to_use = visit_indices[0];
                auto second_visit_to_use = visit_indices[1];
                if (first_visit_to_use > second_visit_to_use) {
                    std::swap(first_visit_to_use, second_visit_to_use);
                }

                solver->AddConstraint(solver->MakeLessOrEqual(time_dimension->CumulVar(first_visit_to_use),
                                                              time_dimension->CumulVar(second_visit_to_use)));
                solver->AddConstraint(solver->MakeLessOrEqual(time_dimension->CumulVar(second_visit_to_use),
                                                              time_dimension->CumulVar(first_visit_to_use)));
                solver->AddConstraint(solver->MakeLessOrEqual(model.ActiveVar(first_visit_to_use),
                                                              model.ActiveVar(second_visit_to_use)));
                solver->AddConstraint(solver->MakeLessOrEqual(model.ActiveVar(second_visit_to_use),
                                                              model.ActiveVar(first_visit_to_use)));

                const auto second_vehicle_var_to_use = solver->MakeMax(model.VehicleVar(second_visit_to_use),
                                                                       solver->MakeIntConst(0));
                solver->AddConstraint(
                        solver->MakeLess(model.VehicleVar(first_visit_to_use), second_vehicle_var_to_use));

                ++total_multiple_carer_visits;
            }
        }

        const auto schedule_day = GetScheduleDate();
        auto solver_ptr = model.solver();

        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto &carer = Carer(vehicle);
            const auto &diary_opt = problem_.diary(carer, schedule_day);

            int64 begin_time = 0;
            int64 end_time = 0;
            if (diary_opt.is_initialized()) {
                const auto &diary = diary_opt.get();

                const auto begin_duration = (diary.begin_date_time() - StartHorizon());
                const auto end_duration = (diary.end_date_time() - StartHorizon());
                CHECK(!begin_duration.is_negative()) << carer.sap_number();
                CHECK(!end_duration.is_negative()) << carer.sap_number();

                begin_time = GetAdjustedWorkdayStart(begin_duration);
                end_time = GetAdjustedWorkdayFinish(end_duration);
                CHECK_GE(begin_time, 0) << carer.sap_number();
                CHECK_LE(begin_time, end_time) << carer.sap_number();
                CHECK_LE(begin_duration.total_seconds(), begin_time) << carer.sap_number();
                CHECK_LE(end_duration.total_seconds(), end_time) << carer.sap_number();

                const auto breaks = CreateBreakIntervals(solver_ptr, carer, diary);
                solver_ptr->AddConstraint(
                        solver_ptr->RevAlloc(new BreakConstraint(time_dimension, vehicle, breaks, *this)));

                variable_store_->SetBreakIntervalVars(vehicle, breaks);
            }

            time_dimension->CumulVar(model.Start(vehicle))->SetRange(begin_time, end_time);
            time_dimension->CumulVar(model.End(vehicle))->SetRange(begin_time, end_time);
        }

        printer->operator<<(ProblemDefinition(model.vehicles(),
                                              model.nodes() - 1,
                                              "unknown area",
                                              schedule_day,
                                              visit_time_window_,
                                              break_time_window_,
                                              GetAdjustment()));

        // Adding penalty costs to allow skipping orders.
        auto max_distance = std::numeric_limits<int64>::min();
        const auto max_node = model.nodes() - 1;
        for (operations_research::RoutingModel::NodeIndex source{0}; source < max_node; ++source) {
            for (auto destination = source + 1; destination < max_node; ++destination) {
                const auto distance = Distance(source, destination);
                if (max_distance < distance) {
                    max_distance = distance;
                }
            }
        }

        // override max distance if it is zero or small
        static const decltype(max_distance) MAX_DISTANCE_OVERRIDE = 3600 * 4;
        const int64 kPenalty = std::max(max_distance, MAX_DISTANCE_OVERRIDE);
        for (const auto &visit_bundle : visit_index_) {
            std::vector<operations_research::RoutingModel::NodeIndex> visit_nodes{std::begin(visit_bundle.second),
                                                                                  std::end(visit_bundle.second)};
            model.AddDisjunction(visit_nodes, kPenalty, static_cast<int64>(visit_nodes.size()));
        }

        if (care_continuity_enabled_) {
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
                    rows::SingleStepSolver::CARE_CONTINUITY_DIMENSION);

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
        }

        VLOG(1) << "Finalizing definition of the routing model...";
        const auto start_time_model_closing = std::chrono::high_resolution_clock::now();

        model.CloseModelWithParameters(parameters_);

        const auto end_time_model_closing = std::chrono::high_resolution_clock::now();
        VLOG(1) << boost::format("Definition of the routing model finalized in %1% seconds")
                   % std::chrono::duration_cast<std::chrono::seconds>(end_time_model_closing
                                                                      - start_time_model_closing).count();

        model.AddSearchMonitor(solver_ptr->RevAlloc(new ProgressPrinterMonitor(model, printer)));
        model.AddSearchMonitor(solver_ptr->RevAlloc(new CancelSearchLimit(cancel_token, solver_ptr)));

        if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
            model.AddSearchMonitor(solver_ptr->RevAlloc(new StalledSearchLimit(
                    no_progress_time_limit_.total_milliseconds(),
                    model.solver()
            )));
        }
    }

    std::string SingleStepSolver::GetDescription(const operations_research::RoutingModel &model,
                                                 const operations_research::Assignment &solution) {
        auto description = SolverWrapper::GetDescription(model, solution);

        if (care_continuity_enabled_) {
            boost::accumulators::accumulator_set<double,
                    boost::accumulators::stats<
                            boost::accumulators::tag::mean,
                            boost::accumulators::tag::median,
                            boost::accumulators::tag::variance> > care_continuity_stats;

            for (const auto &user_satisfaction_pair : care_continuity_) {
                care_continuity_stats(solution.Value(user_satisfaction_pair.second));
            }

            description.append((boost::format("Care continuity: mean: %1% median: %2% stddev: %3%\n")
                                % boost::accumulators::mean(care_continuity_stats)
                                % boost::accumulators::median(care_continuity_stats)
                                % std::sqrt(boost::accumulators::variance(care_continuity_stats))).str()
            );
        }

        return description;
    }

    std::shared_ptr<rows::RoutingVariablesStore> SingleStepSolver::variable_store() {
        return variable_store_;
    }

    SingleStepSolver::CareContinuityMetrics::CareContinuityMetrics(const SingleStepSolver &solver,
                                                                   const rows::Carer &carer)
            : values_() {
        const auto visit_it_end = std::end(solver.visit_index_);
        for (auto visit_it = std::begin(solver.visit_index_); visit_it != visit_it_end; ++visit_it) {
            const auto &service_user = solver.User(visit_it->first.service_user());
            if (service_user.IsPreferred(carer)) {
                for (const auto visit_node : visit_it->second) {
                    values_.insert(std::make_pair(visit_node, service_user.Preference(carer)));
                }
            }
        }
    }

    int64 SingleStepSolver::CareContinuityMetrics::operator()(operations_research::RoutingModel::NodeIndex from,
                                                              operations_research::RoutingModel::NodeIndex to) const {
        const auto to_it = values_.find(to);
        if (to_it == std::end(values_)) {
            return 0;
        }
        return to_it->second;
    }
}