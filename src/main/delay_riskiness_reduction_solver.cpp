#include "delay_riskiness_reduction_solver.h"

#include "util/aplication_error.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"
#include "delay_riskiness_constraint.h"

rows::DelayRiskinessReductionSolver::DelayRiskinessReductionSolver(const ProblemData &problem_data,
                                                                   const History &history,
                                                                   const operations_research::RoutingSearchParameters &search_parameters,
                                                                   boost::posix_time::time_duration visit_time_window,
                                                                   boost::posix_time::time_duration break_time_window,
                                                                   boost::posix_time::time_duration begin_end_work_day_adjustment,
                                                                   boost::posix_time::time_duration no_progress_time_limit,
                                                                   int64 dropped_visit_penalty,
                                                                   int64 max_dropped_visits)
        : MetaheuristicSolver(problem_data,
                              search_parameters,
                              std::move(visit_time_window),
                              std::move(break_time_window),
                              std::move(begin_end_work_day_adjustment),
                              std::move(no_progress_time_limit),
                              dropped_visit_penalty,
                              max_dropped_visits),
          history_{history} {}

void rows::DelayRiskinessReductionSolver::BeforeCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) {
    MetaheuristicSolver::BeforeCloseModel(model, printer);

    riskiness_index_ = model.solver()->MakeIntVar(0, kint64max, "riskiness_index");
    std::unique_ptr<DelayTracker> delay_tracker = std::make_unique<DelayTracker>(*this, history_, &model.GetDimensionOrDie(TIME_DIMENSION));
    model.solver()->AddConstraint(model.solver()->RevAlloc(new DelayRiskinessConstraint(riskiness_index_, std::move(delay_tracker))));
    model.AddVariableMinimizedByFinalizer(riskiness_index_);
}

void rows::DelayRiskinessReductionSolver::AfterCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) {
    MetaheuristicSolver::AfterCloseModel(model, printer);

    model.OverrideCostVar(riskiness_index_);
}
