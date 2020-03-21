#include "metaheuristic_solver.h"

#include "util/aplication_error.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"

rows::MetaheuristicSolver::MetaheuristicSolver(const rows::ProblemData &problem_data,
                                               const operations_research::RoutingSearchParameters &search_parameters,
                                               boost::posix_time::time_duration visit_time_window,
                                               boost::posix_time::time_duration break_time_window,
                                               boost::posix_time::time_duration begin_end_work_day_adjustment,
                                               boost::posix_time::time_duration no_progress_time_limit,
                                               int64 dropped_visit_penalty,
                                               int64 max_dropped_visits_threshold)
        : SolverWrapper(problem_data,
                        search_parameters,
                        std::move(visit_time_window),
                        std::move(break_time_window),
                        std::move(begin_end_work_day_adjustment)),
          no_progress_time_limit_{std::move(no_progress_time_limit)},
          dropped_visit_penalty_{dropped_visit_penalty},
          max_dropped_visits_threshold_{max_dropped_visits_threshold} {}

void rows::MetaheuristicSolver::ConfigureModel(operations_research::RoutingModel &model,
                                               const std::shared_ptr<Printer> &printer,
                                               std::shared_ptr<const std::atomic<bool> > cancel_token,
                                               double cost_normalization_factor) {
    SolverWrapper::ConfigureModel(model, printer, cancel_token, cost_normalization_factor);
    AddTravelTime(model);
    AddVisitsHandling(model);
    AddSkillHandling(model);
    AddContinuityOfCare(model);
    AddCarerHandling(model);

    if (max_dropped_visits_threshold_ > 0) {
        AddDroppedVisitsHandling(model);
        LimitDroppedVisits(model, max_dropped_visits_threshold_);
    }

    BeforeCloseModel(model, printer);

    model.CloseModelWithParameters(parameters_);

    AfterCloseModel(model, printer);

    auto solver = model.solver();
    model.AddSearchMonitor(solver->RevAlloc(new ProgressPrinterMonitor(model, index_manager_, problem_data_, printer, cost_normalization_factor)));

//    solution_collector_ = solver->MakeBestValueSolutionCollector(false);
//    solution_collector_->AddObjective(model.CostVar());
//    model.AddSearchMonitor(solution_collector_);

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(solver->RevAlloc(new StalledSearchLimit(
                no_progress_time_limit_.total_milliseconds(), &model, model.solver())));
    }

    model.AddSearchMonitor(solver->RevAlloc(new CancelSearchLimit(cancel_token, solver)));

    const auto schedule_day = GetScheduleDate();
    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown area",
                                          schedule_day,
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));
}

//operations_research::Assignment *rows::MetaheuristicSolver::BestSolution() const {
//    CHECK(solution_collector_ != nullptr);
//    if (solution_collector_->solution_count() == 0) {
//        return nullptr;
//    }
//
//    return solution_collector_->solution(0);
//}

void rows::MetaheuristicSolver::BeforeCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) {

}

void rows::MetaheuristicSolver::AfterCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) {

}

