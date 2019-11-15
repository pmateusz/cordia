#include "third_step_reduction_solver.h"

#include "util/aplication_error.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"

rows::ThirdStepReductionSolver::ThirdStepReductionSolver(const ProblemData &problem_data,
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
          no_progress_time_limit_{std::move(no_progress_time_limit)},
          dropped_visit_penalty_{dropped_visit_penalty},
          max_dropped_visits_{max_dropped_visits} {}

void rows::ThirdStepReductionSolver::ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                                                    operations_research::RoutingModel &model,
                                                    const std::shared_ptr<Printer> &printer,
                                                    std::shared_ptr<const std::atomic<bool> > cancel_token) {
    CHECK_GE(max_dropped_visits_, 0);
    const auto are_visits_optional = max_dropped_visits_ > 0;

    OnConfigureModel(index_manager, model);

    operations_research::Solver *const solver = model.solver();
    AddTravelTime(solver, model, index_manager);
    AddVisitsHandling(solver, model, index_manager);
    AddSkillHandling(solver, model, index_manager);
    AddContinuityOfCare(solver, model, index_manager);

    int64 global_carer_penalty = 0;

    const auto schedule_day = GetScheduleDate();
    auto solver_ptr = model.solver();
    for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        const auto working_hours = problem_data_.TotalWorkingHours(vehicle, schedule_day);
        if (working_hours > boost::posix_time::seconds(0)) {
            int64 local_carer_penalty = working_hours.total_seconds();
            global_carer_penalty = std::max(global_carer_penalty, local_carer_penalty);

            model.SetFixedCostOfVehicle(local_carer_penalty, vehicle);
        }
    }

    AddCarerHandling(solver, model, index_manager);

    std::stringstream penalty_msg;
    penalty_msg << "CarerUsedPenalty: " << global_carer_penalty;
    printer->operator<<(TracingEvent(TracingEventType::Unknown, penalty_msg.str()));

    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown area",
                                          schedule_day,
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));

    if (are_visits_optional) {
        AddDroppedVisitsHandling(solver, model, index_manager);
        LimitDroppedVisits(solver, model, index_manager, max_dropped_visits_);
    }

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
