#include "second_step_solver.h"

#include "util/aplication_error.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "solution_log_monitor.h"
#include "stalled_search_limit.h"
#include "min_dropped_visits_collector.h"
#include "solution_dumper.h"

rows::SecondStepSolver::SecondStepSolver(const rows::Problem &problem,
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
          no_progress_time_limit_(std::move(no_progress_time_limit)),
          last_dropped_visit_penalty_(0),
          solution_collector_{nullptr},
          solution_repository_{std::make_shared<rows::SolutionRepository>()},
          variable_store_{nullptr} {}

void rows::SecondStepSolver::ConfigureModel(operations_research::RoutingModel &model,
                                            const std::shared_ptr<Printer> &printer,
                                            std::shared_ptr<const std::atomic<bool> > cancel_token) {
    OnConfigureModel(model);

    last_dropped_visit_penalty_ = GetDroppedVisitPenalty(model);
    variable_store_ = std::make_shared<rows::RoutingVariablesStore>(model.nodes(), model.vehicles());

    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(this, &rows::SolverWrapper::Distance));

    static const auto START_FROM_ZERO_TIME = false;
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
        const auto visit_start = visit_index_pair.first.datetime().time_of_day();

        // TODO: sort visit indices, but don't remember why...
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

                DCHECK_LT(start_window, end_window);
                DCHECK_EQ((start_window + end_window) / 2, visit_start.total_seconds());
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
            solver->AddConstraint(solver->MakeLess(model.VehicleVar(first_visit_to_use), second_vehicle_var_to_use));

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

            begin_time = GetAdjustedWorkdayStart(diary.begin_time());
            end_time = GetAdjustedWorkdayFinish(diary.end_time());

            const auto breaks = CreateBreakIntervals(solver_ptr, carer, diary);
            for (const auto &break_item : breaks) {
                model.AddIntervalToAssignment(break_item);
            }

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

    for (const auto &visit_bundle : visit_index_) {
        std::vector<operations_research::RoutingModel::NodeIndex> visit_nodes{std::begin(visit_bundle.second),
                                                                              std::end(visit_bundle.second)};
        model.AddDisjunction(visit_nodes, last_dropped_visit_penalty_, static_cast<int64>(visit_nodes.size()));
    }

    model.CloseModelWithParameters(parameters_);
    model.AddSearchMonitor(solver_ptr->RevAlloc(new ProgressPrinterMonitor(model, printer)));
    model.AddSearchMonitor(solver_ptr->RevAlloc(new SolutionLogMonitor(&model, solution_repository_)));
    solution_collector_ = solver_ptr->RevAlloc(new MinDroppedVisitsSolutionCollector(&model));
    model.AddSearchMonitor(solution_collector_);

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(solver_ptr->RevAlloc(new StalledSearchLimit(
                no_progress_time_limit_.total_milliseconds(),
                model.solver()
        )));
    }

    model.AddSearchMonitor(solver_ptr->RevAlloc(new CancelSearchLimit(cancel_token, solver_ptr)));
}

std::shared_ptr<rows::SolutionRepository> rows::SecondStepSolver::solution_repository() {
    return solution_repository_;
}

std::shared_ptr<rows::RoutingVariablesStore> rows::SecondStepSolver::variable_store() {
    return variable_store_;
}

int64 rows::SecondStepSolver::LastDroppedVisitPenalty() const {
    return last_dropped_visit_penalty_;
}

operations_research::Assignment *rows::SecondStepSolver::min_dropped_visit_solution() const {
    CHECK(solution_collector_);
    return solution_collector_->solution(0);
}
