#include <ortools/constraint_solver/routing_flags.h>
#include <ortools/sat/integer_expr.h>

#include "util/aplication_error.h"

#include "instant_transfer_solver.h"
#include "break_constraint.h"
#include "search_monitor.h"

rows::InstantTransferSolver::InstantTransferSolver(const rows::Problem &problem,
                                                   osrm::EngineConfig &config,
                                                   const operations_research::RoutingSearchParameters &search_parameters)
        : SolverWrapper(problem, config, search_parameters) {}

void rows::InstantTransferSolver::ConfigureModel(operations_research::RoutingModel &model,
                                                 const std::shared_ptr<rows::Printer> &printer,
                                                 const std::atomic<bool> &cancel_token) {
    static const auto START_FROM_ZERO_TIME = false;

    printer->operator<<("Loading the model");
    model.SetArcCostEvaluatorOfAllVehicles(
            NewPermanentCallback(this, &rows::InstantTransferSolver::ServiceTimeWithInstantTransfer));
    model.AddDimension(
            NewPermanentCallback(this, &rows::InstantTransferSolver::ServiceTimeWithInstantTransfer),
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

    std::set<operations_research::RoutingModel::NodeIndex> covered_nodes;
    covered_nodes.insert(DEPOT);

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    for (const auto &visit_index_pair : visit_index_) {
        const auto visit_start = visit_index_pair.first.datetime().time_of_day();

        std::vector<operations_research::IntVar *> start_visit_vars;
        std::vector<operations_research::IntVar *> slack_visit_vars;
        std::vector<operations_research::IntVar *> active_visit_vars;
        for (const auto &visit_node : visit_index_pair.second) {
            covered_nodes.insert(visit_node);
            const auto visit_index = model.NodeToIndex(visit_node);
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

            start_visit_vars.push_back(time_dimension->CumulVar(visit_index));
            slack_visit_vars.push_back(time_dimension->SlackVar(visit_index));
            active_visit_vars.push_back(model.ActiveVar(visit_index));
        }

//        const auto visit_index_size = start_visit_vars.size();
//        if (visit_index_size > 1) {
//            CHECK_EQ(visit_index_size, 2);
//
//            const auto max_arrival_vars = solver->MakeMax(start_visit_vars);
//            solver->AddConstraint(solver->MakeLessOrEqual(
//                    max_arrival_vars, solver->MakeSum(start_visit_vars[0], slack_visit_vars[0])));
//            solver->AddConstraint(solver->MakeLessOrEqual(
//                    max_arrival_vars, solver->MakeSum(start_visit_vars[1], slack_visit_vars[1])));
        solver->AddConstraint(solver->MakeEquality(start_visit_vars[0], start_visit_vars[1]));
        const auto min_active_vars = solver->MakeMin(active_visit_vars);
        solver->AddConstraint(solver->MakeLessOrEqual(active_visit_vars[0], min_active_vars));
        solver->AddConstraint(solver->MakeLessOrEqual(active_visit_vars[1], min_active_vars));
//        }
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
    const int64 kPenalty = 10000000;
    for (const auto &visit_bundle : visit_index_) {
        std::vector<operations_research::RoutingModel::NodeIndex> visit_nodes{std::cbegin(visit_bundle.second),
                                                                              std::cend(visit_bundle.second)};
        model.AddDisjunction(visit_nodes, kPenalty, static_cast<int64>(visit_nodes.size()));
    }

    model.CloseModelWithParameters(parameters_);
    model.AddSearchMonitor(solver_ptr->RevAlloc(new SearchMonitor(solver_ptr, &model, printer, cancel_token)));
}

int64 rows::InstantTransferSolver::ServiceTimeWithInstantTransfer(operations_research::RoutingModel::NodeIndex from,
                                                                  operations_research::RoutingModel::NodeIndex to) {
    if (from == DEPOT) {
        return 0;
    }

    return SolverWrapper::ServiceTime(from);
}
