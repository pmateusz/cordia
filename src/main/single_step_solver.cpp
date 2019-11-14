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

        VLOG(1) << "Finalizing definition of the routing model...";
        const auto start_time_model_closing = std::chrono::high_resolution_clock::now();

        model.CloseModelWithParameters(parameters_);

        const auto end_time_model_closing = std::chrono::high_resolution_clock::now();
        VLOG(1) << boost::format("Definition of the routing model finalized in %1% seconds")
                   % std::chrono::duration_cast<std::chrono::seconds>(end_time_model_closing
                                                                      - start_time_model_closing).count();

        auto solver_ptr = model.solver();
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
}