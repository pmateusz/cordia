#include "two_step_solver.h"

#include "util/aplication_error.h"
#include "break_constraint.h"
#include "search_monitor.h"

rows::TwoStepSolver::TwoStepSolver(const rows::Problem &problem,
                                   osrm::EngineConfig &config,
                                   const operations_research::RoutingSearchParameters &search_parameters)
        : SolverWrapper(problem, config, search_parameters) {}

void rows::TwoStepSolver::ConfigureModel(operations_research::RoutingModel &model,
                                         const std::shared_ptr<Printer> &printer,
                                         const std::atomic<bool> &cancel_token) {
    OnConfigureModel();

    static const auto START_FROM_ZERO_TIME = false;

    printer->operator<<("Loading the model");
    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(this, &rows::SolverWrapper::Distance));
    model.AddDimension(NewPermanentCallback(this, &rows::SolverWrapper::ServicePlusTravelTime),
                       SECONDS_IN_DAY,
                       SECONDS_IN_DAY,
                       START_FROM_ZERO_TIME,
                       TIME_DIMENSION);

    operations_research::RoutingDimension *time_dimension
            = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

    if (model.nodes() == 0) {
        throw util::ApplicationError("Model contains no visits.", util::ErrorCode::ERROR);
    }

    const auto schedule_day = NodeToVisit(operations_research::RoutingModel::NodeIndex{1}).datetime().date();
    if (model.nodes() > 1) {
        for (operations_research::RoutingModel::NodeIndex visit_node{2}; visit_node < model.nodes(); ++visit_node) {
            const auto &visit = NodeToVisit(visit_node);
            if (visit.datetime().date() != schedule_day) {
                throw util::ApplicationError("Visits span across multiple days.", util::ErrorCode::ERROR);
            }
        }
    }

    operations_research::Solver *const solver = model.solver();
    time_dimension->CumulVar(model.NodeToIndex(DEPOT))->SetRange(0, SECONDS_IN_DAY);

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    auto total_multiple_carer_visits = 0;
    for (const auto &visit_index_pair : visit_index_) {
        const auto visit_start = visit_index_pair.first.datetime().time_of_day();

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

            const auto first_visit = visit_indices[0];
            const auto second_visit = visit_indices[1];

            const auto max_arrival_vars = solver->MakeMax({time_dimension->CumulVar(first_visit),
                                                           time_dimension->CumulVar(second_visit)});
            solver->AddConstraint(solver->MakeLessOrEqual(
                    max_arrival_vars,
                    solver->MakeSum(time_dimension->CumulVar(first_visit),
                                    time_dimension->SlackVar(first_visit))));
            solver->AddConstraint(solver->MakeLessOrEqual(
                    max_arrival_vars,
                    solver->MakeSum(time_dimension->CumulVar(second_visit),
                                    time_dimension->SlackVar(second_visit))));

            const auto min_active_vars = solver->MakeMin({model.ActiveVar(first_visit),
                                                          model.ActiveVar(second_visit)});
            solver->AddConstraint(solver->MakeLessOrEqual(model.ActiveVar(first_visit), min_active_vars));
            solver->AddConstraint(solver->MakeLessOrEqual(model.ActiveVar(second_visit), min_active_vars));

            solver->AddConstraint(solver->MakeLess(
                    solver->MakeConditionalExpression(
                            solver->MakeIsDifferentCstVar(model.VehicleVar(first_visit), -1),
                            model.VehicleVar(first_visit), 0),
                    solver->MakeConditionalExpression(
                            solver->MakeIsDifferentCstVar(model.VehicleVar(second_visit), -1),
                            model.VehicleVar(second_visit), 1)
            ));

            solver->AddConstraint(
                    solver->MakeAllDifferentExcept({model.VehicleVar(first_visit),
                                                    model.VehicleVar(second_visit)},
                                                   -1));

            ++total_multiple_carer_visits;
        }
    }

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

    // Adding penalty costs to allow skipping orders.
    auto max_distance = std::numeric_limits<int64>::min();
    const auto max_node = model.nodes() - 1;
    for (operations_research::RoutingModel::NodeIndex source{0}; source < max_node; ++source) {
        for (auto destination = source + 1; destination < max_node; ++destination) {
            const auto distance = Distance(source, destination);
            if (max_distance < distance) {
                max_distance = distance;
            }
        }
    }

    const int64 kPenalty = max_distance / 6;
    LOG(INFO) << "Penalty: " << kPenalty;

    for (const auto &visit_bundle : visit_index_) {
        std::vector<operations_research::RoutingModel::NodeIndex> visit_nodes{std::cbegin(visit_bundle.second),
                                                                              std::cend(visit_bundle.second)};
        model.AddDisjunction(visit_nodes, kPenalty, static_cast<int64>(visit_nodes.size()));
    }

    model.CloseModelWithParameters(parameters_);
    model.AddSearchMonitor(solver_ptr->RevAlloc(new SearchMonitor(solver_ptr, &model, printer, cancel_token)));
}
