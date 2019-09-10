#include "third_step_reduction_solver.h"

#include "util/aplication_error.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "solution_log_monitor.h"
#include "stalled_search_limit.h"

rows::ThirdStepReductionSolver::ThirdStepReductionSolver(const rows::Problem &problem,
                                                         osrm::EngineConfig &config,
                                                         const operations_research::RoutingSearchParameters &search_parameters,
                                                         boost::posix_time::time_duration visit_time_window,
                                                         boost::posix_time::time_duration break_time_window,
                                                         boost::posix_time::time_duration begin_end_work_day_adjustment,
                                                         boost::posix_time::time_duration no_progress_time_limit,
                                                         int64 dropped_visit_penalty,
                                                         int64 max_dropped_visits,
                                                         std::vector<RouteValidatorBase::Metrics> vehicle_metrics)
        : SolverWrapper(problem,
                        config,
                        search_parameters,
                        std::move(visit_time_window),
                        std::move(break_time_window),
                        std::move(begin_end_work_day_adjustment)),
          no_progress_time_limit_{std::move(no_progress_time_limit)},
          dropped_visit_penalty_{dropped_visit_penalty},
          max_dropped_visits_{max_dropped_visits},
          vehicle_metrics_{std::move(vehicle_metrics)} {}

void rows::ThirdStepReductionSolver::ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                                                    operations_research::RoutingModel &model,
                                                    const std::shared_ptr<Printer> &printer,
                                                    std::shared_ptr<const std::atomic<bool> > cancel_token) {
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
    static const auto START_FROM_ZERO_TIME = false;
    model.AddDimension(service_time_callback_handle,
                       SECONDS_IN_DIMENSION,
                       SECONDS_IN_DIMENSION,
                       START_FROM_ZERO_TIME,
                       TIME_DIMENSION);

    const auto FIXED_COST = 4 * 3600;
    model.SetFixedCostOfAllVehicles(FIXED_COST);
//    int max_available_time = 0;
//    for (const auto &metric : vehicle_metrics_) {
//        max_available_time = std::max(static_cast<int64>(max_available_time), static_cast<int64>(metric.available_time().total_seconds()));
//    }
//    CHECK_GT(max_available_time, 0);
//    for (auto vehicle_number = 0; vehicle_number < vehicle_metrics_.size(); ++vehicle_number) {
//        const auto vehicle_metrics = vehicle_metrics_[vehicle_number];
//        if (vehicle_metrics.available_time().total_seconds() > 0) {
//            const auto working_time_fraction = static_cast<double>(vehicle_metrics.available_time().total_seconds()) / max_available_time;
//            CHECK_GT(working_time_fraction, 0.0);
//            const auto vehicle_cost = static_cast<int64>(FIXED_COST / working_time_fraction);
//            model.SetFixedCostOfVehicle(vehicle_cost, vehicle_number);
//        }
//    }

    operations_research::RoutingDimension *time_dimension = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

    operations_research::Solver *const solver = model.solver();
    time_dimension->CumulVar(index_manager.NodeToIndex(DEPOT))->SetRange(0, SECONDS_IN_DIMENSION);

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    auto total_multiple_carer_visits = 0;
    for (const auto &visit_index_pair : visit_index_) {
        const auto visit_start = visit_index_pair.first.datetime() - StartHorizon();
        CHECK(!visit_start.is_negative()) << visit_index_pair.first.id();

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
            solver->AddConstraint(solver->MakeLess(model.VehicleVar(first_visit_to_use), second_vehicle_var_to_use));

            ++total_multiple_carer_visits;
        }
    }

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

            begin_time = GetAdjustedWorkdayStart(diary.begin_time());
            end_time = GetAdjustedWorkdayFinish(diary.end_time());

            const auto breaks = CreateBreakIntervals(solver_ptr, carer, diary);
            time_dimension->SetBreakIntervalsOfVehicle(breaks, vehicle, service_times);
        }
        time_dimension->CumulVar(model.Start(vehicle))->SetRange(begin_time, end_time);
        time_dimension->CumulVar(model.End(vehicle))->SetRange(begin_time, end_time);
    }
    solver_ptr->AddConstraint(solver_ptr->RevAlloc(new operations_research::GlobalVehicleBreaksConstraint(time_dimension)));

    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown area",
                                          schedule_day,
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));

    CHECK_GE(max_dropped_visits_, 0);
    if (max_dropped_visits_ > 0) {
        for (const auto &visit_bundle : visit_index_) {
            std::vector<int64> visit_indices = index_manager.NodesToIndices(visit_bundle.second);
            model.AddDisjunction(visit_indices, dropped_visit_penalty_, static_cast<int64>(visit_indices.size()));
        }
    }

    std::vector<operations_research::IntVar *> all_visits;
    for (const auto &visit_bundle : visit_index_) {
        const auto visit_node = *std::begin(visit_bundle.second);
        all_visits.push_back(model.VehicleVar(index_manager.NodeToIndex(visit_node)));
    }
    solver->AddConstraint(solver->MakeAtMost(all_visits, -1, max_dropped_visits_));

    model.CloseModelWithParameters(parameters_);
    model.AddSearchMonitor(solver_ptr->RevAlloc(new ProgressPrinterMonitor(model, printer)));

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(solver_ptr->RevAlloc(new StalledSearchLimit(
                no_progress_time_limit_.total_milliseconds(),
                &model,
                model.solver()
        )));
    }

    model.AddSearchMonitor(solver_ptr->RevAlloc(new CancelSearchLimit(cancel_token, solver_ptr)));
}
