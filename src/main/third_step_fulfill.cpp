#include "third_step_fulfill.h"

#include "util/aplication_error.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"

rows::ThirdStepFulfillSolver::ThirdStepFulfillSolver(const ProblemData &problem_data,
                                                     const operations_research::RoutingSearchParameters &search_parameters,
                                                     boost::posix_time::time_duration visit_time_window,
                                                     boost::posix_time::time_duration break_time_window,
                                                     boost::posix_time::time_duration begin_end_work_day_adjustment,
                                                     boost::posix_time::time_duration no_progress_time_limit,
                                                     int64 dropped_visit_penalty,
                                                     int64 max_dropped_visits,
                                                     std::vector<RouteValidatorBase::Metrics> vehicle_metrics)
        : SolverWrapper(problem_data,
                        search_parameters,
                        std::move(visit_time_window),
                        std::move(break_time_window),
                        std::move(begin_end_work_day_adjustment)),
          no_progress_time_limit_{std::move(no_progress_time_limit)},
          dropped_visit_penalty_{dropped_visit_penalty},
          max_dropped_visits_{max_dropped_visits},
          vehicle_metrics_{std::move(vehicle_metrics)} {}

void rows::ThirdStepFulfillSolver::ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                                                  operations_research::RoutingModel &model,
                                                  const std::shared_ptr<Printer> &printer,
                                                  std::shared_ptr<const std::atomic<bool> > cancel_token) {
    OnConfigureModel(index_manager, model);

    operations_research::Solver *const solver = model.solver();
    AddTravelTime(solver, model, index_manager);

    const auto FIXED_COST = 5 * 3600;
    for (decltype(vehicle_metrics_.size()) vehicle_number = 0; vehicle_number < vehicle_metrics_.size(); ++vehicle_number) {
        const auto vehicle_metrics = vehicle_metrics_[vehicle_number];
        if (vehicle_metrics.available_time().total_seconds() > 0) {
            const auto working_time_fraction = static_cast<double>(vehicle_metrics.travel_time().total_seconds() +
                                                                   vehicle_metrics.service_time().total_seconds())
                                               / vehicle_metrics.available_time().total_seconds();
            CHECK_GT(working_time_fraction, 0.0);
            const auto vehicle_cost = static_cast<int64>(FIXED_COST / working_time_fraction);
            CHECK_GT(vehicle_cost, 0.0);
            model.SetFixedCostOfVehicle(vehicle_cost, vehicle_number);
        }
        // when comparing with mip the cost of a vehicle should be set to zero
        // model.SetFixedCostOfVehicle(0, vehicle_number);
    }

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    AddVisitsHandling(solver, model, index_manager);
    AddSkillHandling(solver, model, index_manager);
    AddContinuityOfCare(solver, model, index_manager);
    AddCarerHandling(solver, model, index_manager);

    const auto schedule_day = GetScheduleDate();
    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown area",
                                          schedule_day,
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));

    CHECK_GE(max_dropped_visits_, 0);
    if (max_dropped_visits_ > 0) {
        AddDroppedVisitsHandling(solver, model, index_manager);
    }
    LimitDroppedVisits(solver, model, index_manager, max_dropped_visits_);


    model.CloseModelWithParameters(parameters_);
    model.AddSearchMonitor(solver->RevAlloc(new ProgressPrinterMonitor(model, printer)));

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(solver->RevAlloc(new StalledSearchLimit(
                no_progress_time_limit_.total_milliseconds(),
                &model,
                model.solver()
        )));
    }

    model.AddSearchMonitor(solver->RevAlloc(new CancelSearchLimit(cancel_token, solver)));
}
