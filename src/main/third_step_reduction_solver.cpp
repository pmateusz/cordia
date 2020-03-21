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
        : MetaheuristicSolver(problem_data,
                              search_parameters,
                              std::move(visit_time_window),
                              std::move(break_time_window),
                              std::move(begin_end_work_day_adjustment),
                              std::move(no_progress_time_limit),
                              dropped_visit_penalty,
                              max_dropped_visits) {}

void rows::ThirdStepReductionSolver::BeforeCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) {
    MetaheuristicSolver::BeforeCloseModel(model, printer);

    int64 global_carer_penalty = 0;
    const auto schedule_day = GetScheduleDate();

    for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        const auto working_hours = problem_data_.TotalWorkingHours(vehicle, schedule_day);
        if (working_hours > boost::posix_time::seconds(0)) {
            int64 local_carer_penalty = working_hours.total_seconds();
            global_carer_penalty = std::max(global_carer_penalty, local_carer_penalty);

            model.SetFixedCostOfVehicle(local_carer_penalty, vehicle);
        }
    }

    std::stringstream penalty_msg;
    penalty_msg << "CarerUsedPenalty: " << global_carer_penalty;
    printer->operator<<(TracingEvent(TracingEventType::Unknown, penalty_msg.str()));
}
