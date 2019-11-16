#include "second_step_solver.h"

#include "util/aplication_error.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "solution_log_monitor.h"
#include "stalled_search_limit.h"
#include "min_dropped_visits_collector.h"

rows::SecondStepSolver::SecondStepSolver(const ProblemData &problem_data,
                                         const operations_research::RoutingSearchParameters &search_parameters,
                                         boost::posix_time::time_duration visit_time_window,
                                         boost::posix_time::time_duration break_time_window,
                                         boost::posix_time::time_duration begin_end_work_day_adjustment,
                                         boost::posix_time::time_duration no_progress_time_limit)
        : SolverWrapper(problem_data,
                        search_parameters,
                        std::move(visit_time_window),
                        std::move(break_time_window),
                        std::move(begin_end_work_day_adjustment)),
          no_progress_time_limit_(std::move(no_progress_time_limit)),
          solution_collector_{nullptr},
          solution_repository_{std::make_shared<rows::SolutionRepository>()} {}

void rows::SecondStepSolver::ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                                            operations_research::RoutingModel &model,
                                            const std::shared_ptr<Printer> &printer,
                                            std::shared_ptr<const std::atomic<bool> > cancel_token,
                                            double cost_normalization_factor) {
    OnConfigureModel(index_manager, model);

    operations_research::Solver *const solver = model.solver();

    AddTravelTime(solver, model, index_manager);
    AddVisitsHandling(solver, model, index_manager);
    AddSkillHandling(solver, model, index_manager);
    AddContinuityOfCare(solver, model, index_manager);
    AddCarerHandling(solver, model, index_manager);
    AddDroppedVisitsHandling(solver, model, index_manager);

    const auto schedule_day = GetScheduleDate();
    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown area",
                                          schedule_day,
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));

    model.CloseModelWithParameters(parameters_);

    model.AddSearchMonitor(solver->RevAlloc(new ProgressPrinterMonitor(model, printer, cost_normalization_factor)));
    model.AddSearchMonitor(solver->RevAlloc(new SolutionLogMonitor(&index_manager, &model, solution_repository_)));
    solution_collector_ = solver->RevAlloc(new MinDroppedVisitsSolutionCollector(&model, true));
    model.AddSearchMonitor(solution_collector_);

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(solver->RevAlloc(new StalledSearchLimit(
                no_progress_time_limit_.total_milliseconds(),
                &model,
                model.solver()
        )));
    }

    model.AddSearchMonitor(solver->RevAlloc(new CancelSearchLimit(cancel_token, solver)));
}

std::shared_ptr<rows::SolutionRepository> rows::SecondStepSolver::solution_repository() {
    return solution_repository_;
}
