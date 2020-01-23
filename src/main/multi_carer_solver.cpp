#include "multi_carer_solver.h"

#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"

rows::MultiCarerSolver::MultiCarerSolver(const rows::ProblemData &problem_data,
                                         const operations_research::RoutingSearchParameters &search_parameters,
                                         boost::posix_time::time_duration visit_time_window,
                                         boost::posix_time::time_duration break_time_window,
                                         boost::posix_time::time_duration begin_end_work_day_adjustment,
                                         boost::posix_time::time_duration no_progress_time_limit)
        : SolverWrapper(problem_data,
                        search_parameters,
                        std::move(visit_time_window),
                        std::move(break_time_window),
                        std::move(begin_end_work_day_adjustment)),
          no_progress_time_limit_(std::move(no_progress_time_limit)),
          solution_collector_{nullptr} {
}

void rows::MultiCarerSolver::ConfigureModel(operations_research::RoutingModel &model,
                                            const std::shared_ptr<Printer> &printer,
                                            std::shared_ptr<const std::atomic<bool> > cancel_token,
                                            double cost_normalization_factor) {
    static const auto START_FROM_ZERO_TIME = false;

    OnConfigureModel(model);

    solution_collector_ = model.solver()->MakeBestValueSolutionCollector(false);

    operations_research::Solver *const solver = model.solver();

    AddTravelTime(model);

    operations_research::RoutingDimension *time_dimension = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
    time_dimension->CumulVar(index_manager_.NodeToIndex(RealProblemData::DEPOT))->SetRange(0, RealProblemData::SECONDS_IN_DIMENSION);

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    std::vector<operations_research::IntVar *> window_cost_components;

    // need to handle gracefully -1
    std::vector<int64> num_skills_by_vehicle{0};
    for (auto vehicle = 0; vehicle < vehicles(); ++vehicle) {
        num_skills_by_vehicle.push_back(Carer(vehicle).skills().size());
    }

    const auto VISIT_NOT_MADE_PENALTY = visit_time_window_.total_seconds() + 1;
    auto total_multiple_carer_visits = 0;

    for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
        const auto &visit = problem_data_.NodeToVisit(visit_node);

        const auto visit_start = visit.datetime() - StartHorizon();
        DCHECK(!visit_start.is_negative()) << visit.id();

        std::vector<int64> visit_indices;
        for (const auto local_visit_node : problem_data_.GetNodes(visit)) {
            const auto visit_index = index_manager_.NodeToIndex(local_visit_node);
            visit_indices.push_back(visit_index);

            if (HasTimeWindows()) {
                const auto start_window = GetBeginVisitWindow(visit_start);
                const auto end_window = GetEndVisitWindow(visit_start);

                time_dimension
                        ->CumulVar(visit_index)
                        ->SetRange(start_window, end_window);

                DCHECK_LT(start_window, end_window) << visit.id();
                DCHECK_LE(start_window, visit_start.total_seconds()) << visit.id();
                DCHECK_LE(visit_start.total_seconds(), end_window) << visit.id();
            } else {
                time_dimension->CumulVar(visit_index)->SetValue(visit_start.total_seconds());
            }
            model.AddToAssignment(time_dimension->CumulVar(visit_index));
            model.AddToAssignment(time_dimension->SlackVar(visit_index));

            solution_collector_->Add(time_dimension->CumulVar(visit_index));
        }

        const auto visit_indices_size = visit_indices.size();
        if (visit_indices_size > 1) {
            CHECK_EQ(visit_indices_size, 2);

            auto first_visit_to_use = visit_indices[0];
            auto second_visit_to_use = visit_indices[1];
            if (first_visit_to_use > second_visit_to_use) {
                std::swap(first_visit_to_use, second_visit_to_use);
            }

            window_cost_components.emplace_back(solver->MakeAbs(solver->MakeDifference(time_dimension->CumulVar(first_visit_to_use),
                                                                                       time_dimension->CumulVar(
                                                                                               second_visit_to_use))->Var())->Var());

            window_cost_components.emplace_back(
                    solver->MakeProd(
                            solver->MakeDifference(2,
                                                   solver->MakeSum(model.ActiveVar(first_visit_to_use), model.ActiveVar(second_visit_to_use))),
                            VISIT_NOT_MADE_PENALTY)->Var());

            const auto second_vehicle_var_to_use = solver->MakeMax(model.VehicleVar(second_visit_to_use), solver->MakeIntConst(0));
            solver->AddConstraint(solver->MakeLess(model.VehicleVar(first_visit_to_use), second_vehicle_var_to_use));

            ++total_multiple_carer_visits;
        }

        for (auto visit_index : visit_indices) {
            window_cost_components.push_back(
                    solver->MakeElement(num_skills_by_vehicle, solver->MakeSum(model.VehicleVar(visit_index), 1)->Var())->Var());
        }
    }

    AddSkillHandling(model);
    AddContinuityOfCare(model);
    AddCarerHandling(model);

    const auto schedule_day = GetScheduleDate();
    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown area",
                                          schedule_day,
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));

//     override max distance if it is zero or small
    AddDroppedVisitsHandling(model, VISIT_NOT_MADE_PENALTY);

    VLOG(1) << "Finalizing definition of the routing model...";
    const auto start_time_model_closing = std::chrono::high_resolution_clock::now();

    for (const auto &component : window_cost_components) {
        model.AddVariableMinimizedByFinalizer(component);
    }

    auto objective = solver->MakeSum(window_cost_components)->Var();
    model.AddToAssignment(objective);
    solution_collector_->AddObjective(objective);
    solution_collector_->Add(objective);
    solution_collector_->Add(model.Nexts());

    model.CloseModelWithParameters(parameters_);
    model.OverrideCostVar(objective);

    const auto end_time_model_closing = std::chrono::high_resolution_clock::now();
    VLOG(1) << boost::format("Definition of the routing model finalized in %1% seconds")
               % std::chrono::duration_cast<std::chrono::seconds>(end_time_model_closing - start_time_model_closing).count();


    model.AddSearchMonitor(solution_collector_);
    model.AddSearchMonitor(solver->RevAlloc(new ProgressPrinterMonitor(model, printer, cost_normalization_factor)));
    model.AddSearchMonitor(solver->RevAlloc(new CancelSearchLimit(cancel_token, solver)));

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(solver->RevAlloc(new StalledSearchLimit(no_progress_time_limit_.total_milliseconds(), &model, model.solver())));
    }
}

operations_research::Assignment *rows::MultiCarerSolver::GetBestSolution() const {
    const auto solution_count = solution_collector_->solution_count();
    if (solution_count > 0) {
        return solution_collector_->solution(0);
    }
    return nullptr;
}
