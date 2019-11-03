#include <chrono>
#include <utility>

#include "util/aplication_error.h"

#include "single_step_solver.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"
#include "min_dropped_visits_collector.h"

namespace rows {

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
              no_progress_time_limit_(no_progress_time_limit) {}

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

    void SingleStepSolver::ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                                          operations_research::RoutingModel &model,
                                          const std::shared_ptr<Printer> &printer,
                                          std::shared_ptr<const std::atomic<bool> > cancel_token) {
        static const auto START_FROM_ZERO_SERVICE_SATISFACTION = true;
        static const auto START_FROM_ZERO_TIME = false;

        OnConfigureModel(index_manager, model);

        const auto transit_callback_handle = model.RegisterTransitCallback(
                [this, &index_manager](int64 from_index, int64 to_index) -> int64 {
                    return this->Distance(index_manager.IndexToNode(from_index), index_manager.IndexToNode(to_index));
                });
        model.SetArcCostEvaluatorOfAllVehicles(transit_callback_handle);

        const auto service_time_callback_handle = model.RegisterTransitCallback(
                [this, &index_manager](int64 from_index, int64 to_index) -> int64 {
                    return this->ServicePlusTravelTime(index_manager.IndexToNode(from_index),
                                                       index_manager.IndexToNode(to_index));
                });
        model.AddDimension(service_time_callback_handle,
                           SECONDS_IN_DIMENSION,
                           SECONDS_IN_DIMENSION,
                           START_FROM_ZERO_TIME,
                           TIME_DIMENSION);

        operations_research::RoutingDimension *time_dimension
                = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

        operations_research::Solver *const solver = model.solver();
        time_dimension->CumulVar(index_manager.NodeToIndex(DEPOT))->SetRange(0, SECONDS_IN_DIMENSION);

        // visit that needs multiple carers is referenced by multiple nodes
        // all such nodes must be either performed or unperformed
        auto total_multiple_carer_visits = 0;
        for (const auto &visit_index_pair : visit_index_) {
            const auto visit_start = visit_index_pair.first.datetime() - StartHorizon();
            DCHECK(!visit_start.is_negative()) << visit_index_pair.first.id();

            std::vector<int64> visit_indices;
            for (const auto &visit_node : visit_index_pair.second) {
                const auto visit_index = index_manager.NodeToIndex(visit_node);
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

        AddSkillHandling(solver, model, index_manager);
        AddContinuityOfCare(solver, model, index_manager);

        // could be interesting to use the Google constraint for breaks
        // initial results show violation of some breaks
        std::vector<int64> service_times(model.Size());
        for (int node = 0; node < model.Size(); node++) {
            if (node >= model.nodes() || node == 0) {
                service_times[node] = 0;
            } else {
                const auto &visit = visit_by_node_.at(node);
                service_times[node] = visit.duration().total_seconds();
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
                CHECK_GE(begin_duration.total_seconds(), begin_time) << carer.sap_number(); // TODO: should be GE
                CHECK_LE(end_duration.total_seconds(), end_time) << carer.sap_number();

                const auto breaks = CreateBreakIntervals(solver_ptr, carer, diary);
                time_dimension->SetBreakIntervalsOfVehicle(breaks, vehicle, service_times);
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
        const auto max_travel_times = location_container_.LargestDistances(3);
        const auto max_distance = std::accumulate(std::cbegin(max_travel_times), std::cend(max_travel_times), static_cast<int64>(0));

        // override max distance if it is zero or small
        for (const auto &visit_bundle : visit_index_) {
            std::vector<int64> visit_indices = index_manager.NodesToIndices(visit_bundle.second);
            model.AddDisjunction(visit_indices, max_distance, static_cast<int64>(visit_indices.size()));
        }

        VLOG(1) << "Finalizing definition of the routing model...";
        const auto start_time_model_closing = std::chrono::high_resolution_clock::now();

        model.CloseModelWithParameters(parameters_);

        const auto end_time_model_closing = std::chrono::high_resolution_clock::now();
        VLOG(1) << boost::format("Definition of the routing model finalized in %1% seconds")
                   % std::chrono::duration_cast<std::chrono::seconds>(end_time_model_closing
                                                                      - start_time_model_closing).count();

        model.AddSearchMonitor(solver_ptr->RevAlloc(new ProgressPrinterMonitor(model, printer)));
        model.AddSearchMonitor(solver_ptr->RevAlloc(new MinDroppedVisitsSolutionCollector(&model, true)));
        model.AddSearchMonitor(solver_ptr->RevAlloc(new CancelSearchLimit(cancel_token, solver_ptr)));

        if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
            model.AddSearchMonitor(solver_ptr->RevAlloc(new StalledSearchLimit(
                    no_progress_time_limit_.total_milliseconds(),
                    &model,
                    model.solver()
            )));
        }
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

    int64 SingleStepSolver::CareContinuityMetrics::operator()(operations_research::RoutingNodeIndex from,
                                                              operations_research::RoutingNodeIndex to) const {
        const auto to_it = values_.find(to);
        if (to_it == std::end(values_)) {
            return 0;
        }
        return to_it->second;
    }
}