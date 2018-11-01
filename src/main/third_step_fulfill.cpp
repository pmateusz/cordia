#include "third_step_fulfill.h"

#include "util/aplication_error.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "solution_log_monitor.h"
#include "stalled_search_limit.h"

rows::ThirdStepFulfillSolver::ThirdStepFulfillSolver(const rows::Problem &problem,
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

void rows::ThirdStepFulfillSolver::ConfigureModel(operations_research::RoutingModel &model,
                                                  const std::shared_ptr<Printer> &printer,
                                                  std::shared_ptr<const std::atomic<bool> > cancel_token) {
    OnConfigureModel(model);

    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(this, &rows::SolverWrapper::Distance));

    static const auto START_FROM_ZERO_TIME = false;
    model.AddDimension(NewPermanentCallback(this, &rows::SolverWrapper::ServicePlusTravelTime),
                       SECONDS_IN_DIMENSION,
                       SECONDS_IN_DIMENSION,
                       START_FROM_ZERO_TIME,
                       TIME_DIMENSION);

    const auto FIXED_COST = 5 * 3600;
    for (auto vehicle_number = 0; vehicle_number < vehicle_metrics_.size(); ++vehicle_number) {
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
    }

    operations_research::RoutingDimension *time_dimension
            = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

    operations_research::Solver *const solver = model.solver();
    time_dimension->CumulVar(model.NodeToIndex(DEPOT))->SetRange(0, SECONDS_IN_DIMENSION);

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    auto total_multiple_carer_visits = 0;
    for (const auto &visit_index_pair : visit_index_) {
        const auto visit_start = visit_index_pair.first.datetime() - StartHorizon();
        CHECK(!visit_start.is_negative()) << visit_index_pair.first.id();

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

    const auto schedule_day = GetScheduleDate();
    auto solver_ptr = model.solver();
    for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        const auto &carer = Carer(vehicle);
        const auto &diary_opt = problem_.diary(carer, schedule_day);

        int64 begin_time = 0;
        int64 end_time = 0;
        if (diary_opt.is_initialized()) {
            const auto &diary = diary_opt.get();

            const auto begin_duration = (diary.begin_date_time() - StartHorizon());
            const auto end_duration = (diary.end_date_time() - StartHorizon());
            CHECK(!begin_duration.is_negative()) << carer.sap_number();
            CHECK(!end_duration.is_negative()) << carer.sap_number();

            begin_time = GetAdjustedWorkdayStart(begin_duration);
            end_time = GetAdjustedWorkdayFinish(end_duration);
            CHECK_GE(begin_time, 0) << carer.sap_number();
            CHECK_LT(begin_time, end_time) << carer.sap_number();
            CHECK_LE(begin_time, begin_duration.total_seconds()) << carer.sap_number();
            CHECK_LE(end_duration.total_seconds(), end_time) << carer.sap_number();

            const auto breaks = CreateBreakIntervals(solver_ptr, carer, diary);
            solver_ptr->AddConstraint(
                    solver_ptr->RevAlloc(new BreakConstraint(time_dimension, vehicle, breaks, *this)));
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

    if (max_dropped_visits_ > 0) {
        for (const auto &visit_bundle : visit_index_) {
            std::vector<operations_research::RoutingModel::NodeIndex> visit_nodes{std::begin(visit_bundle.second),
                                                                                  std::end(visit_bundle.second)};
            model.AddDisjunction(visit_nodes, dropped_visit_penalty_, static_cast<int64>(visit_nodes.size()));
        }

        std::vector<operations_research::IntVar *> visit_nodes;
        for (const auto &visit_bundle : visit_index_) {
            const auto visit_node = *std::begin(visit_bundle.second);
            visit_nodes.push_back(model.VehicleVar(model.NodeToIndex(visit_node)));
        }
        solver->AddConstraint(solver->MakeAtMost(visit_nodes, -1, max_dropped_visits_));
    } else {
        model.AddAllActive();
    }

    model.CloseModelWithParameters(parameters_);
    model.AddSearchMonitor(solver_ptr->RevAlloc(new ProgressPrinterMonitor(model, printer)));

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(solver_ptr->RevAlloc(new StalledSearchLimit(
                no_progress_time_limit_.total_milliseconds(),
                model.solver()
        )));
    }

    model.AddSearchMonitor(solver_ptr->RevAlloc(new CancelSearchLimit(cancel_token, solver_ptr)));
}
