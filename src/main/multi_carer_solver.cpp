#include "multi_carer_solver.h"

#include "progress_printer_monitor.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"

rows::MultiCarerSolver::MultiCarerSolver(const rows::Problem &problem,
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
          no_progress_time_limit_(std::move(no_progress_time_limit)),
          solution_collector_{nullptr} {
}

void rows::MultiCarerSolver::ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                                            operations_research::RoutingModel &model,
                                            const std::shared_ptr<Printer> &printer,
                                            std::shared_ptr<const std::atomic<bool> > cancel_token) {
    static const auto START_FROM_ZERO_TIME = false;

    OnConfigureModel(index_manager, model);

    solution_collector_ = model.solver()->MakeBestValueSolutionCollector(false);

    const auto transit_callback_handle = model.RegisterTransitCallback(
            [this, &index_manager](int64 from_index, int64 to_index) -> int64 {
                return this->Distance(index_manager.IndexToNode(from_index), index_manager.IndexToNode(to_index));
            });
    model.SetArcCostEvaluatorOfAllVehicles(transit_callback_handle);

    const auto service_time_callback_handle = model.RegisterTransitCallback(
            [this, &index_manager](int64 from_index, int64 to_index) -> int64 {
                return this->ServicePlusTravelTime(index_manager.IndexToNode(from_index), index_manager.IndexToNode(to_index));
            });
    model.AddDimension(service_time_callback_handle, SECONDS_IN_DIMENSION, SECONDS_IN_DIMENSION, START_FROM_ZERO_TIME, TIME_DIMENSION);
    operations_research::RoutingDimension *time_dimension = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

    operations_research::Solver *const solver = model.solver();
    time_dimension->CumulVar(index_manager.NodeToIndex(DEPOT))->SetRange(0, SECONDS_IN_DIMENSION);

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    std::vector<operations_research::IntVar *> window_cost_components;

    const auto VISIT_NOT_MADE_PENALTY = visit_time_window_.total_seconds() + 1;
    auto total_multiple_carer_visits = 0;
    for (const auto &visit_index_pair : visit_index_) {
        const auto visit_start = visit_index_pair.first.datetime() - StartHorizon();
        DCHECK(!visit_start.is_negative()) << visit_index_pair.first.id();

        std::vector<int64> visit_indices;
        for (const auto &visit_node : visit_index_pair.second) {
            const auto visit_index = index_manager.NodeToIndex(visit_node);
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

//            solver->AddConstraint(solver->MakeEquality(time_dimension->CumulVar(first_visit_to_use), time_dimension->CumulVar(second_visit_to_use)));
//            solver->AddConstraint(solver->MakeEquality(model.ActiveVar(first_visit_to_use), model.ActiveVar((second_visit_to_use))));

            window_cost_components.emplace_back(
                    solver->MakeAbs(solver->MakeDifference(time_dimension->CumulVar(first_visit_to_use),
                                                           time_dimension->CumulVar(second_visit_to_use))->Var())->Var());

            window_cost_components.emplace_back(
                    solver->MakeProd(
                            solver->MakeDifference(2,
                                                   solver->MakeSum(model.ActiveVar(first_visit_to_use), model.ActiveVar(second_visit_to_use))),
                            VISIT_NOT_MADE_PENALTY)->Var());

            const auto second_vehicle_var_to_use = solver->MakeMax(model.VehicleVar(second_visit_to_use), solver->MakeIntConst(0));
            solver->AddConstraint(solver->MakeLess(model.VehicleVar(first_visit_to_use), second_vehicle_var_to_use));

            ++total_multiple_carer_visits;
        }
    }

    const auto schedule_day = GetScheduleDate();
    auto solver_ptr = model.solver();

    // could be interesting to use the Google constraint for breaks
    // initial results show violation of some breaks
    std::vector<int64> service_times(model.Size());
    for (std::size_t node = 0; node < model.Size(); node++) {
        if (node >= model.nodes() || node == 0) {
            service_times[node] = 0;
        } else {
            const auto &visit = visit_by_node_.at(node);
            service_times[node] = visit.duration().total_seconds();
        }
    }

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
            CHECK_LE(begin_time, end_time) << carer.sap_number();
            CHECK_GE(begin_duration.total_seconds(), begin_time) << carer.sap_number(); // TODO: should be GE
            CHECK_LE(end_duration.total_seconds(), end_time) << carer.sap_number();

            const auto breaks = CreateBreakIntervals(solver_ptr, carer, diary);
            time_dimension->SetBreakIntervalsOfVehicle(breaks, vehicle, service_times);
        }

        time_dimension->CumulVar(model.Start(vehicle))->SetRange(begin_time, end_time);
        time_dimension->CumulVar(model.End(vehicle))->SetRange(begin_time, end_time);
    }
    solver_ptr->AddConstraint(solver_ptr->RevAlloc(new operations_research::GlobalVehicleBreaksConstraint(time_dimension)));

    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown area",
                                          schedule_day,
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));

//     override max distance if it is zero or small
    for (const auto &visit_bundle : visit_index_) {
        std::vector<int64> visit_indices = index_manager.NodesToIndices(visit_bundle.second);
        model.AddDisjunction(visit_indices, VISIT_NOT_MADE_PENALTY, static_cast<int64>(visit_indices.size()));
    }

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
               % std::chrono::duration_cast<std::chrono::seconds>(end_time_model_closing
                                                                  - start_time_model_closing).count();


    model.AddSearchMonitor(solution_collector_);
    model.AddSearchMonitor(solver_ptr->RevAlloc(new ProgressPrinterMonitor(model, printer)));
    model.AddSearchMonitor(solver_ptr->RevAlloc(new CancelSearchLimit(cancel_token, solver_ptr)));

    if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
        model.AddSearchMonitor(solver_ptr->RevAlloc(new StalledSearchLimit(no_progress_time_limit_.total_milliseconds(), &model, model.solver())));
    }
}

operations_research::Assignment *rows::MultiCarerSolver::GetBestSolution() const {
    const auto solution_count = solution_collector_->solution_count();
    if (solution_count > 0) {
        return solution_collector_->solution(0);
    }
    return nullptr;
}