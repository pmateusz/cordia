#include <random>
#include <unordered_map>

#include <boost/optional.hpp>

#include "experimental_enforcement_worker.h"
#include "gexf_writer.h"
#include "progress_printer_monitor.h"
#include "break_constraint.h"
#include "stalled_search_limit.h"
#include "cancel_search_limit.h"
#include "routing_operations.h"
#include "constraint_operations.h"
#include "multiple_visit_query.h"

rows::ExperimentalEnforcementWorker::Solver::Solver(const rows::Problem &problem,
                                                    osrm::EngineConfig &config,
                                                    const operations_research::RoutingSearchParameters &search_parameters,
                                                    boost::posix_time::time_duration visit_time_window,
                                                    boost::posix_time::time_duration break_time_window,
                                                    boost::posix_time::time_duration begin_end_work_day_adjustment_time_window)
        : SolverWrapper(problem,
                        config,
                        search_parameters,
                        std::move(visit_time_window),
                        std::move(break_time_window),
                        std::move(begin_end_work_day_adjustment_time_window)) {}

void rows::ExperimentalEnforcementWorker::Solver::ConfigureModel(operations_research::RoutingModel &model,
                                                                 const std::shared_ptr<rows::Printer> &printer,
                                                                 std::shared_ptr<const std::atomic<bool> > cancel_token) {
    OnConfigureModel(model);

    static const auto START_FROM_ZERO_TIME = false;

    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(this, &rows::SolverWrapper::Distance));
    model.AddDimension(NewPermanentCallback(this, &rows::SolverWrapper::ServicePlusTravelTime),
                       SECONDS_IN_DAY,
                       SECONDS_IN_DAY,
                       START_FROM_ZERO_TIME,
                       TIME_DIMENSION);

    auto time_dimension = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);

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

            auto first_visit_to_use = visit_indices[0];
            auto second_visit_to_use = visit_indices[1];
            if (first_visit_to_use > second_visit_to_use) {
                std::swap(first_visit_to_use, second_visit_to_use);
            }
            // CAUTION - it ceased to remain valid with symmetry fixing
            model.solver()->AddConstraint(model.solver()->MakeLessOrEqual(time_dimension->CumulVar(first_visit_to_use),
                                                                          time_dimension->CumulVar(second_visit_to_use)
            ));
//            solver->AddConstraint(solver->MakeLessOrEqual(time_dimension->CumulVar(second_visit_to_use),
//                                                          time_dimension->CumulVar(first_visit_to_use)));
            model.solver()->AddConstraint(model.solver()->MakeLessOrEqual(model.ActiveVar(first_visit_to_use),
                                                                          model.ActiveVar(second_visit_to_use)
            ));
//            solver->AddConstraint(solver->MakeLessOrEqual(model.ActiveVar(second_visit_to_use),
//                                                          model.ActiveVar(first_visit_to_use)));

//            const auto second_vehicle_var_to_use = model.solver()->MakeMax(model.VehicleVar(second_visit_to_use),
//                                                                           model.solver()->MakeIntConst(1));
//            model.solver()->AddConstraint(
//                    model.solver()->MakeLess(model.VehicleVar(first_visit_to_use), second_vehicle_var_to_use));

            model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(first_visit_to_use));
            model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(second_visit_to_use));
        }
    }

    const auto schedule_day = GetScheduleDate();
    for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        const auto &carer = Carer(vehicle);
        const auto &diary_opt = problem_.diary(carer, schedule_day);

        int64 begin_time = 0;
        int64 begin_time_to_use = 0;
        int64 end_time = 0;
        int64 end_time_to_use = 0;
        if (diary_opt.is_initialized()) {
            const auto &diary = diary_opt.get();

            begin_time = diary.begin_time().total_seconds();
            end_time = diary.end_time().total_seconds();
            begin_time_to_use = GetAdjustedWorkdayStart(diary.begin_time());
            end_time_to_use = GetAdjustedWorkdayFinish(diary.end_time());

            const auto breaks = CreateBreakIntervals(model.solver(), carer, diary);
            model.solver()->AddConstraint(
                    model.solver()->RevAlloc(new rows::BreakConstraint(time_dimension, vehicle, breaks, *this)));
//            time_dimension->SetBreakIntervalsOfVehicle(breaks, vehicle);
        }

        time_dimension->CumulVar(model.Start(vehicle))->SetRange(begin_time_to_use, end_time);
        time_dimension->CumulVar(model.End(vehicle))->SetRange(begin_time, end_time_to_use);
    }

    printer->operator<<(ProblemDefinition(model.vehicles(),
                                          model.nodes() - 1,
                                          "unknown",
                                          schedule_day,
                                          visit_time_window_,
                                          break_time_window_,
                                          GetAdjustment()));

    const int64 kPenalty = GetDroppedVisitPenalty(model);
    for (const auto &visit_bundle : visit_index_) {
        std::vector<operations_research::RoutingModel::NodeIndex> visit_nodes{std::begin(visit_bundle.second),
                                                                              std::end(visit_bundle.second)};
        if (visit_nodes.size() == 1) {
            model.AddDisjunction(visit_nodes, kPenalty);
        } else {
            model.AddDisjunction(visit_nodes,
                                 static_cast<int64>(1.5 * kPenalty),
                                 static_cast<int64>(visit_nodes.size()));
        }
    }

    model.CloseModelWithParameters(parameters_);
    model.AddSearchMonitor(model.solver()->RevAlloc(new rows::ProgressPrinterMonitor(model, printer)));
    model.AddSearchMonitor(model.solver()->RevAlloc(new rows::CancelSearchLimit(cancel_token, model.solver())));

//    static const int64 MEGA_BYTE = 1024 * 1024;
//    static const int64 GIGA_BYTE = MEGA_BYTE * 1024;
//    model.AddSearchMonitor(solver_ptr->RevAlloc(new MemoryLimitSearchMonitor(16 * GIGA_BYTE, solver_ptr)));
    model.AddSearchMonitor(model.solver()->RevAlloc(new rows::StalledSearchLimit(30 * 1000, model.solver())));
}

rows::ExperimentalEnforcementWorker::ExperimentalEnforcementWorker(std::shared_ptr<rows::Printer> printer)
        : printer_{std::move(printer)} {}

bool rows::ExperimentalEnforcementWorker::Init(rows::Problem problem,
                                               osrm::EngineConfig routing_params,
                                               const operations_research::RoutingSearchParameters &search_params,
                                               std::string output_file) {
    problem_ = std::move(problem);
    routing_params_ = std::move(routing_params);
    search_params_ = search_params;
    output_file_ = std::move(output_file);
    return true;
}

struct EnforcedConstraints {
    EnforcedConstraints()
            : Symmetry{false},
              BothActiveOrNone{false},
              StartAtTheSameTime{false} {}

    bool Symmetry;
    bool BothActiveOrNone;
    bool StartAtTheSameTime;
};

void rows::ExperimentalEnforcementWorker::Run() {
    static const boost::filesystem::path CACHED_ASSIGNMENT_PATH{"cached_assignment.pb"};

    try {
        std::unique_ptr<Solver> solver_wrapper
                = std::make_unique<Solver>(problem_,
                                           routing_params_,
                                           search_params_,
                                           boost::posix_time::minutes(120),
                                           boost::posix_time::minutes(120),
                                           boost::posix_time::minutes(15));

        std::unique_ptr<operations_research::RoutingModel> model
                = std::make_unique<operations_research::RoutingModel>(solver_wrapper->nodes(),
                                                                      solver_wrapper->vehicles(),
                                                                      rows::SolverWrapper::DEPOT);

        solver_wrapper->ConfigureModel(*model, printer_, CancelToken());

        operations_research::Assignment const *assignment = nullptr;
        if (boost::filesystem::exists(CACHED_ASSIGNMENT_PATH)) {
            assignment = model->ReadAssignment(CACHED_ASSIGNMENT_PATH.string());
            if (assignment) {
                LOG(INFO) << "Loaded previous assignment";
            }
        }

        if (!assignment) {
            assignment = model->SolveWithParameters(search_params_);

            LOG(INFO) << "Search completed"
                      << "\nLocal search profile: " << model->solver()->LocalSearchProfile()
                      << "\nDebug string: " << model->solver()->DebugString()
                      << "\nModel status: " << solver_wrapper->GetModelStatus(model->status());

            if (assignment == nullptr) {
                throw util::ApplicationError("No solution found.", util::ErrorCode::ERROR);
            }

            CHECK(assignment->Save(CACHED_ASSIGNMENT_PATH.string())) << "Failed to save the solution";

            operations_research::Assignment validation_copy{assignment};
            const auto is_solution_correct = model->solver()->CheckAssignment(&validation_copy);
            CHECK(is_solution_correct);
        }

        operations_research::Assignment *patched_assignment = nullptr;
        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > local_routes;
        model->AssignmentToRoutes(*assignment, &local_routes);

        const auto time_dim = model->GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
        patched_assignment = model->ReadAssignmentFromRoutes(local_routes, true);
        CHECK(model->solver()->CheckAssignment(patched_assignment)) << "Assignment is not valid";

        std::default_random_engine generator;
        rows::ConstraintOperations constraint_operations{*solver_wrapper, *model};
        static const rows::RoutingOperations routing_operations{};
        std::unordered_map<rows::CalendarVisit,
                EnforcedConstraints,
                rows::Problem::PartialVisitOperations,
                rows::Problem::PartialVisitOperations> enforced_constraints;

        static const auto AVOID_SYMMETRY = true;
        rows::MultipleVisitQuery query{*solver_wrapper, *model, patched_assignment, AVOID_SYMMETRY};
        while (true) {
            std::vector<rows::CalendarVisit> relaxed_visits = problem_.Visits(
                    [&query](const rows::CalendarVisit &visit) -> bool { return query.IsRelaxed(visit); }
            );

            LOG(INFO) << "Visits relaxed: " << relaxed_visits.size();
            if (relaxed_visits.empty()) {
                break;
            }

            std::vector<rows::CalendarVisit> satisfied_visits = problem_.Visits(
                    [&query](const rows::CalendarVisit &visit) -> bool { return query.IsSatisfied(visit); }
            );

            int enforced_visits = 0;
            for (const auto &visit : satisfied_visits) {
                const auto nodes = solver_wrapper->GetNodePair(visit);
                const auto first_index = model->NodeToIndex(nodes.first);
                const auto second_index = model->NodeToIndex(nodes.second);

                auto find_it = enforced_constraints.find(visit);
                if (find_it == std::end(enforced_constraints)) {
                    auto insert_pair = enforced_constraints.insert({visit, EnforcedConstraints()});
                    CHECK(insert_pair.second);
                    find_it = insert_pair.first;
                    ++enforced_visits;
                }

                if (!find_it->second.StartAtTheSameTime) {
                    find_it->second.StartAtTheSameTime = true;
                    constraint_operations.FirstVehicleArrivesNoLaterThanSecond(first_index, second_index);
                }

                if (!find_it->second.BothActiveOrNone) {
                    find_it->second.BothActiveOrNone = true;
                    constraint_operations.FirstVisitIsActiveIfSecondIs(first_index, second_index);
                }

                if (!find_it->second.Symmetry) {
                    find_it->second.Symmetry = true;
                    constraint_operations.FirstVehicleNumberIsSmaller(first_index, second_index);
                }
            }

            LOG(INFO) << "Visits closed: " << enforced_visits;

            const auto visit_fraction = static_cast<std::size_t>(std::ceil(progress_fraction_ * relaxed_visits.size()));
            CHECK_GT(visit_fraction, 0);
            std::vector<rows::CalendarVisit> relaxed_visits_to_use{visit_fraction};
            if (visit_fraction == 1) {
                std::uniform_int_distribution<> distribution(0, static_cast<int>(relaxed_visits.size() - 1));
                relaxed_visits_to_use[0] = relaxed_visits[distribution(generator)];
            } else {
                std::vector<std::size_t> indices(relaxed_visits.size());
                const auto relaxed_visit_size = relaxed_visits.size();
                for (auto index = 0u; index < relaxed_visit_size; ++index) {
                    indices[index] = index;
                }

                std::shuffle(std::begin(indices), std::end(indices), generator);
                for (auto index = 0; index < visit_fraction; ++index) {
                    relaxed_visits_to_use[index] = relaxed_visits[indices[index]];
                }
            }

            local_routes.clear();
            model->AssignmentToRoutes(*patched_assignment, &local_routes);
            for (const auto &relaxed_visit: relaxed_visits_to_use) {
                const auto nodes = solver_wrapper->GetNodePair(relaxed_visit);
                const auto first_index = model->NodeToIndex(nodes.first);
                const auto second_index = model->NodeToIndex(nodes.second);
                CHECK_LT(first_index, second_index);
                auto first_vehicle = patched_assignment->Min(model->VehicleVar(first_index));
                auto second_vehicle = patched_assignment->Min(model->VehicleVar(second_index));
                if (first_vehicle != -1 && second_vehicle != -1) {
                    if (first_vehicle > second_vehicle) {
                        CHECK_EQ(routing_operations.Remove(local_routes, model->IndexToNode(first_index)), 1);
                        CHECK_EQ(routing_operations.Remove(local_routes, model->IndexToNode(second_index)), 1);
                        constraint_operations.FirstVehicleNumberIsSmaller(first_index, second_index);
                        LOG(INFO) << "Patched symmetry violation";
                    } else if (first_vehicle == second_vehicle) {
                        CHECK_EQ(routing_operations.Remove(local_routes, model->IndexToNode(first_index)), 1);
                        CHECK_EQ(routing_operations.Remove(local_routes, model->IndexToNode(second_index)), 1);
                        constraint_operations.FirstVehicleNumberIsSmaller(first_index, second_index);
                        LOG(INFO) << "Patched the same vehicle for both visits";
                    } else if (patched_assignment->Min(time_dim->CumulVar(first_index))
                               != patched_assignment->Min(time_dim->CumulVar(second_index))) {
                        CHECK_EQ(routing_operations.Remove(local_routes, model->IndexToNode(first_index)), 1);
                        CHECK_EQ(routing_operations.Remove(local_routes, model->IndexToNode(second_index)), 1);
                        LOG(INFO) << "Patched different arrival times";
                        constraint_operations.FirstVehicleArrivesNoLaterThanSecond(first_index, second_index);
                    } else if (query.IsSatisfied(relaxed_visit)) {
                        LOG(INFO) << "Relaxed visit became valid";

                        auto find_it = enforced_constraints.find(relaxed_visit);
                        if (find_it == std::end(enforced_constraints)) {
                            auto insert_pair = enforced_constraints.insert({relaxed_visit, EnforcedConstraints()});
                            CHECK(insert_pair.second);
                            find_it = insert_pair.first;
                            ++enforced_visits;
                        }

                        if (!find_it->second.StartAtTheSameTime) {
                            find_it->second.StartAtTheSameTime = true;
                            constraint_operations.FirstVehicleArrivesNoLaterThanSecond(first_index, second_index);
                        }

                        if (!find_it->second.BothActiveOrNone) {
                            find_it->second.BothActiveOrNone = true;
                            constraint_operations.FirstVisitIsActiveIfSecondIs(first_index, second_index);
                        }

                        if (!find_it->second.Symmetry) {
                            find_it->second.Symmetry = true;
                            constraint_operations.FirstVehicleNumberIsSmaller(first_index, second_index);
                        }
                    } else {
                        LOG(FATAL) << "Unknown constraint violation";
                    }
                } else if (!(first_vehicle == -1 && second_vehicle == -1)) {
                    CHECK_EQ(routing_operations.Remove(local_routes, model->IndexToNode(second_index)), 1);
                    constraint_operations.FirstVisitIsActiveIfSecondIs(first_index, second_index);
                    LOG(INFO) << "Patched only one visit is being served";
                } else {
                    LOG(FATAL) << "Unknown constraint violation";
                }

                patched_assignment = model->ReadAssignmentFromRoutes(local_routes, true);
                CHECK(model->solver()->CheckAssignment(patched_assignment)) << "Assignment became invalid";
                query.SetAssignment(patched_assignment);
            }

            auto local_assignment = model->SolveFromAssignmentWithParameters(patched_assignment, search_params_);
            CHECK(local_assignment);

            // TODO: record value of objective functions
            // TODO: record number of dropped visits
            patched_assignment = model->solver()->MakeAssignment(local_assignment);
            query.SetAssignment(patched_assignment);
        }

        // TODO: slow progression of the bound
        rows::GexfWriter solution_writer;
        solution_writer.Write(output_file_, *solver_wrapper, *model, *patched_assignment, boost::none);
        solver_wrapper->DisplayPlan(*model, *patched_assignment);
        SetReturnCode(STATUS_OK);
    } catch (util::ApplicationError &ex) {
        LOG(ERROR) << ex.msg() << std::endl << ex.diagnostic_info();
        SetReturnCode(util::to_exit_code(ex.error_code()));
    } catch (const std::exception &ex) {
        LOG(ERROR) << ex.what() << std::endl;
        SetReturnCode(2);
    } catch (...) {
        LOG(ERROR) << "Unhandled exception";
        SetReturnCode(3);
    }
}