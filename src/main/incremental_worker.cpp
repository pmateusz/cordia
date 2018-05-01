#include "incremental_worker.h"

#include <glog/logging.h>

#include "gexf_writer.h"


int Remove(std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > &routes,
           operations_research::RoutingModel::NodeIndex node) {
    auto changes = 0;
    for (auto &route : routes) {
        while (true) {
            auto find_it = std::find(std::begin(route), std::end(route), node);
            if (find_it == std::end(route)) {
                break;
            }

            route.erase(find_it);
            ++changes;
        }
    }
    return changes;
}

int Swap(std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > &routes,
         operations_research::RoutingModel::NodeIndex left,
         operations_research::RoutingModel::NodeIndex right) {
    auto changed = 0;
    for (auto &route : routes) {
        const auto route_size = route.size();
        for (auto route_index = 0; route_index < route_size; ++route_index) {
            if (route[route_index] == left) {
                route[route_index] = right;
                ++changed;
            } else if (route[route_index] == right) {
                route[route_index] = left;
                ++changed;
            }
        }
    }
    changed;
}

int Replace(std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > &routes,
            operations_research::RoutingModel::NodeIndex from,
            operations_research::RoutingModel::NodeIndex to,
            std::size_t route_index) {
    auto changed = 0;
    auto &route = routes[route_index];
    const auto route_size = route.size();
    for (auto node_index = 0; node_index < route_size; ++node_index) {
        if (route[node_index] == from) {
            route[node_index] = to;
            ++changed;
        }
    }
    return changed;
}

class IsRelaxedQuery {
public:
    IsRelaxedQuery(rows::SolverWrapper &solver_wrapper,
                   operations_research::RoutingModel &model,
                   operations_research::Assignment const *solution)
            : solver_wrapper_(solver_wrapper),
              model_(model),
              time_dim_(model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION)),
              solution_(solution) {}

    bool operator()(const rows::CalendarVisit &visit) const {
        if (visit.carer_count() < 2) {
            return false;
        }

        const auto &nodes = solver_wrapper_.GetNodePair(visit);
        const auto first_index = model_.NodeToIndex(nodes.first);
        const auto second_index = model_.NodeToIndex(nodes.second);
        const auto first_vehicle = solution_->Min(model_.VehicleVar(first_index));
        const auto second_vehicle = solution_->Min(model_.VehicleVar(second_index));

        if (first_vehicle == -1 && second_vehicle == -1) {
            return false;
        }

        if (first_vehicle == second_vehicle) {
            // both visits are assigned to the same carer
            return true;
        }

        if ((first_vehicle == -1 && second_vehicle != -1) || (first_vehicle != -1 && second_vehicle == -1)) {
            // only one visit is performed
            return true;
        }

        if (first_vehicle > second_vehicle) {
            // symmetry violation
            return true;
        }

        CHECK_LT(first_vehicle, second_vehicle);

        // different arrival time
        return solution_->Min(time_dim_->CumulVar(first_index)) != solution_->Min(time_dim_->CumulVar(second_index));
    }

private:
    const rows::SolverWrapper &solver_wrapper_;
    const operations_research::RoutingModel &model_;
    operations_research::RoutingDimension const *time_dim_;
    operations_research::Assignment const *solution_;
};

void rows::IncrementalSchedulingWorker::Run() {
    static const boost::filesystem::path CACHED_ASSIGNMENT_PATH{"cached_assignment.pb"};

    try {
        std::unique_ptr<rows::IncrementalSolver> solver_wrapper
                = std::make_unique<rows::IncrementalSolver>(problem_,
                                                            routing_params_,
                                                            search_params_,
                                                            boost::posix_time::minutes(120),
                                                            true);

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

        ConstraintOperations constraint_operations{*solver_wrapper, *model};
        while (true) {
            std::vector<rows::CalendarVisit> relaxed_visits;
            {
                IsRelaxedQuery query{*solver_wrapper, *model, patched_assignment};
                relaxed_visits = problem_.Visits(query);
            }

            if (relaxed_visits.empty()) {
                break;
            }

            local_routes.clear();
            model->AssignmentToRoutes(*patched_assignment, &local_routes);

            LOG(INFO) << "Relaxed visits: " << relaxed_visits.size();
            for (const auto &relaxed_visit: relaxed_visits) {
                const auto nodes = solver_wrapper->GetNodePair(relaxed_visit);
                const auto first_index = model->NodeToIndex(nodes.first);
                const auto second_index = model->NodeToIndex(nodes.second);
                CHECK_LT(first_index, second_index);
                auto first_vehicle = patched_assignment->Min(model->VehicleVar(first_index));
                auto second_vehicle = patched_assignment->Min(model->VehicleVar(second_index));
                if (first_vehicle != -1 && second_vehicle != -1) {
                    if (first_vehicle > second_vehicle) {
                        CHECK_EQ(Remove(local_routes, model->IndexToNode(first_index)), 1);
                        CHECK_EQ(Remove(local_routes, model->IndexToNode(second_index)), 1);
                        constraint_operations.FirstVehicleNumberIsSmaller(first_index, second_index);
                    } else if (first_vehicle == second_vehicle) {
                        CHECK_EQ(Remove(local_routes, model->IndexToNode(first_index)), 1);
                        CHECK_EQ(Remove(local_routes, model->IndexToNode(second_index)), 1);
                        constraint_operations.FirstVehicleNumberIsSmaller(first_index, second_index);
                    } else if (patched_assignment->Min(time_dim->CumulVar(first_index))
                               != patched_assignment->Min(time_dim->CumulVar(second_index))) {
                        CHECK_EQ(Remove(local_routes, model->IndexToNode(first_index)), 1);
                        CHECK_EQ(Remove(local_routes, model->IndexToNode(second_index)), 1);
                        constraint_operations.FirstVehicleArrivesNoLaterThanSecond(first_index, second_index);
                    }
                } else if (!(first_vehicle == -1 && second_vehicle == -1)) {
                    constraint_operations.FirstVisitIsActiveIfSecondIs(first_index, second_index);
                } else {
                    LOG(FATAL) << "Unknown constraint violation";
                }

                patched_assignment = model->ReadAssignmentFromRoutes(local_routes, true);
                CHECK(model->solver()->CheckAssignment(patched_assignment)) << "Assignment became invalid";
            }

            auto local_assignment = model->SolveFromAssignmentWithParameters(patched_assignment, search_params_);
            CHECK(local_assignment);
            patched_assignment = model->solver()->MakeAssignment(local_assignment);
        }

        // TODO: slow progression of the bound
        rows::GexfWriter solution_writer;
        solution_writer.Write(output_file_, *solver_wrapper, *model, *patched_assignment);
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

rows::IncrementalSchedulingWorker::IncrementalSchedulingWorker(std::shared_ptr<rows::Printer> printer)
        : printer_{std::move(printer)} {}

bool rows::IncrementalSchedulingWorker::Init(rows::Problem problem,
                                             osrm::EngineConfig routing_params,
                                             const operations_research::RoutingSearchParameters &search_params,
                                             std::string output_file) {
    problem_ = std::move(problem);
    routing_params_ = std::move(routing_params);
    search_params_ = search_params;
    output_file_ = std::move(output_file);
    return true;
}

void rows::IncrementalSchedulingWorker::PrintRoutes(
        const std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > &routes) const {
    for (const auto &route : routes) {
        std::vector<std::string> node_strings;
        for (const auto node : route) {
            node_strings.emplace_back(std::to_string(node.value()));
        }

        printer_->operator<<(boost::algorithm::join(node_strings, " -> "));
    }
}

void rows::IncrementalSchedulingWorker::PrintMultipleCarerVisits(const operations_research::Assignment &assignment,
                                                                 const operations_research::RoutingModel &model,
                                                                 const rows::SolverWrapper &solver_wrapper) const {
    const auto time_dimension_ptr = model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
    for (const auto &visit : this->problem_.visits()) {
        if (visit.carer_count() <= 1) {
            continue;
        }

        const auto &nodes = solver_wrapper.GetNodes(visit);
        CHECK_EQ(nodes.size(), 2);

        auto node_it = std::begin(nodes);
        const auto first_visit_node = *node_it;
        const auto second_visit_node = *std::next(node_it);

        auto first_visit_index = model.NodeToIndex(first_visit_node);
        auto second_visit_index = model.NodeToIndex(second_visit_node);
        const auto first_vehicle = assignment.Min(model.VehicleVar(first_visit_index));
        const auto second_vehicle = assignment.Min(model.VehicleVar(second_visit_index));
        const auto first_time = assignment.Min(time_dimension_ptr->CumulVar(first_visit_index));
        const auto second_time = assignment.Min(time_dimension_ptr->CumulVar(second_visit_index));
        const auto status = (first_vehicle != -1
                             && second_vehicle != -1
                             && first_vehicle != second_vehicle
                             && first_time == second_time);

        printer_->operator<<((boost::format(
                "Visit %3d %3d - [%2d %2d] [%2d %2d] - [%6d %6d] [%6d %6d] - [%2d %2d] - [%6d %6d] [%6d %6d] - %3d")
                              % first_visit_node
                              % second_visit_node
                              % assignment.Min(model.VehicleVar(first_visit_index))
                              % assignment.Min(model.VehicleVar(second_visit_index))
                              % assignment.Max(model.VehicleVar(first_visit_index))
                              % assignment.Max(model.VehicleVar(second_visit_index))
                              % assignment.Min(time_dimension_ptr->CumulVar(first_visit_index))
                              % assignment.Min(time_dimension_ptr->CumulVar(second_visit_index))
                              % assignment.Max(time_dimension_ptr->CumulVar(first_visit_index))
                              % assignment.Max(time_dimension_ptr->CumulVar(second_visit_index))
                              % assignment.Min(model.ActiveVar(first_visit_index))
                              % assignment.Min(model.ActiveVar(second_visit_index))
                              % assignment.Min(time_dimension_ptr->SlackVar(first_visit_index))
                              % assignment.Min(time_dimension_ptr->SlackVar(second_visit_index))
                              % assignment.Max(time_dimension_ptr->SlackVar(first_visit_index))
                              % assignment.Max(time_dimension_ptr->SlackVar(second_visit_index))
                              % status).str());
    }
}

rows::IncrementalSchedulingWorker::ConstraintOperations::ConstraintOperations(rows::SolverWrapper &solver_wrapper,
                                                                              operations_research::RoutingModel &routing_model)
        : solver_wrapper_{solver_wrapper},
          model_{routing_model},
          time_dim_{routing_model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION)} {}

void rows::IncrementalSchedulingWorker::ConstraintOperations::FirstVehicleNumberIsSmaller(int64 first_node,
                                                                                          int64 second_node) {
    model_.solver()->AddConstraint(
            model_.solver()->MakeLess(model_.VehicleVar(first_node),
                                      model_.solver()->MakeMax(model_.VehicleVar(second_node),
                                                               model_.solver()->MakeIntConst(1))));
}

void rows::IncrementalSchedulingWorker::ConstraintOperations::FirstVisitIsActiveIfSecondIs(int64 first_node,
                                                                                           int64 second_node) {
    model_.solver()->AddConstraint(model_.solver()->MakeLessOrEqual(model_.ActiveVar(second_node),
                                                                    model_.ActiveVar(first_node)));
}

void rows::IncrementalSchedulingWorker::ConstraintOperations::FirstVehicleArrivesNoLaterThanSecond(int64 first_node,
                                                                                                   int64 second_node) {
    model_.solver()->AddConstraint(model_.solver()->MakeLessOrEqual(time_dim_->CumulVar(second_node),
                                                                    time_dim_->CumulVar(first_node)));
}
