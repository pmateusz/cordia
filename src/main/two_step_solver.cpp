#include "two_step_solver.h"

#include "util/aplication_error.h"
#include "break_constraint.h"
#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "solution_dumper.h"

rows::TwoStepSolver::TwoStepSolver(const rows::Problem &problem,
                                   osrm::EngineConfig &config,
                                   const operations_research::RoutingSearchParameters &search_parameters)
        : SolverWrapper(problem, config, search_parameters) {}

void rows::TwoStepSolver::ConfigureModel(operations_research::RoutingModel &model,
                                         const std::shared_ptr<Printer> &printer,
                                         std::shared_ptr<const std::atomic<bool> > cancel_token) {
    OnConfigureModel(model);

    printer->operator<<("Loading the model");
    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(this, &rows::SolverWrapper::Distance));

    static const auto START_FROM_ZERO_TIME = false;
    model.AddDimension(NewPermanentCallback(this, &rows::SolverWrapper::ServicePlusTravelTime),
                       SECONDS_IN_DAY,
                       SECONDS_IN_DAY,
                       START_FROM_ZERO_TIME,
                       TIME_DIMENSION);

    operations_research::RoutingDimension *time_dimension
            = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

    operations_research::Solver *const solver = model.solver();
    time_dimension->CumulVar(model.NodeToIndex(DEPOT))->SetRange(0, SECONDS_IN_DAY);

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    auto total_multiple_carer_visits = 0;
    for (const auto &visit_index_pair : visit_index_) {
        const auto visit_start = visit_index_pair.first.datetime().time_of_day();

        // TODO: sort visit indices
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

            begin_time = GetAdjustedWorkdayStart(diary.begin_time());
            end_time = GetAdjustedWorkdayFinish(diary.end_time());

            const auto breaks = CreateBreakIntervals(solver_ptr, carer, diary);
            solver_ptr->AddConstraint(
                    solver_ptr->RevAlloc(new BreakConstraint(time_dimension, vehicle, breaks, *this)));
        }

        time_dimension->CumulVar(model.Start(vehicle))->SetRange(begin_time, end_time);
        time_dimension->CumulVar(model.End(vehicle))->SetRange(begin_time, end_time);
    }

    printer->operator<<(ProblemDefinition(model.vehicles(), model.nodes() - 1, visit_time_window_, 0));

    const int64 kPenalty = GetDroppedVisitPenalty(model);
    for (const auto &visit_bundle : visit_index_) {
        std::vector<operations_research::RoutingModel::NodeIndex> visit_nodes{std::cbegin(visit_bundle.second),
                                                                              std::cend(visit_bundle.second)};
        model.AddDisjunction(visit_nodes, kPenalty, static_cast<int64>(visit_nodes.size()));
    }

    model.CloseModelWithParameters(parameters_);
    model.AddSearchMonitor(
            solver_ptr->RevAlloc(new SolutionDumper(boost::filesystem::path("/home/pmateusz/dev/cordia/"),
                                                    "solution%1%.pb",
                                                    model)));
    model.AddSearchMonitor(solver_ptr->RevAlloc(new ProgressPrinterMonitor(model, printer)));
    model.AddSearchMonitor(solver_ptr->RevAlloc(new CancelSearchLimit(cancel_token, solver_ptr)));
}
