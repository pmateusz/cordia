#include "delay_probability_reduction_solver.h"
#include "third_step_solver.h"

#include "util/aplication_error.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"
#include "delay_probability_constraint.h"

rows::DelayProbabilityReductionSolver::DelayProbabilityReductionSolver(const ProblemData &problem_data,
                                                                       const History &history,
                                                                       const operations_research::RoutingSearchParameters &search_parameters,
                                                                       boost::posix_time::time_duration visit_time_window,
                                                                       boost::posix_time::time_duration break_time_window,
                                                                       boost::posix_time::time_duration begin_end_work_day_adjustment,
                                                                       boost::posix_time::time_duration no_progress_time_limit,
                                                                       int64 dropped_visit_penalty,
                                                                       int64 max_dropped_visits)
        : SolverWrapper(problem_data,
                        search_parameters,
                        std::move(visit_time_window),
                        std::move(break_time_window),
                        std::move(begin_end_work_day_adjustment)),
          history_{history},
          no_progress_time_limit_{std::move(no_progress_time_limit)},
          dropped_visit_penalty_{dropped_visit_penalty},
          max_dropped_visits_{max_dropped_visits} {}

void rows::DelayProbabilityReductionSolver::ConfigureModel(operations_research::RoutingModel &model,
                                                           const std::shared_ptr<Printer> &printer,
                                                           std::shared_ptr<const std::atomic<bool> > cancel_token,
                                                           double cost_normalization_factor) {
    CHECK_GE(max_dropped_visits_, 0);
    const auto are_visits_optional = max_dropped_visits_ > 0;

    SolverWrapper::ConfigureModel(model, printer, cancel_token, cost_normalization_factor);
    AddTravelTime(model);
    AddVisitsHandling(model);
    AddSkillHandling(model);
    AddContinuityOfCare(model);
    AddCarerHandling(model);

    auto delay_probability_var = model.solver()->MakeIntVar(0, 100, "delay_probability");
    model.solver()->AddConstraint(
            model.solver()->RevAlloc(new DelayProbabilityConstraint(
                    delay_probability_var,
                    std::make_unique<DelayTracker>(*this, history_, &model.GetDimensionOrDie(TIME_DIMENSION)))));
    model.AddVariableMinimizedByFinalizer(delay_probability_var);

    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown area",
                                          GetScheduleDate(),
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));

    if (are_visits_optional) {
        AddDroppedVisitsHandling(model);
        LimitDroppedVisits(model, max_dropped_visits_);
    }

    model.CloseModelWithParameters(parameters_);
    model.OverrideCostVar(delay_probability_var);
    model.AddSearchMonitor(model.solver()->RevAlloc(new ProgressPrinterMonitor(model, printer, cost_normalization_factor)));

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(model.solver()->RevAlloc(new StalledSearchLimit(
                no_progress_time_limit_.total_milliseconds(),
                &model,
                model.solver()
        )));
    }

    model.AddSearchMonitor(model.solver()->RevAlloc(new CancelSearchLimit(cancel_token, model.solver())));
}
