#include "multiple_visit_query.h"

rows::MultipleVisitQuery::MultipleVisitQuery(rows::SolverWrapper &solver_wrapper,
                                             operations_research::RoutingIndexManager &index_manager,
                                             operations_research::RoutingModel &model,
                                             operations_research::Assignment const *solution, bool avoid_symmetry)
        : solver_wrapper_(solver_wrapper),
          index_manager_(index_manager),
          model_(model),
          time_dim_(model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION)),
          solution_(solution),
          avoid_symmetry_(avoid_symmetry) {}

bool rows::MultipleVisitQuery::IsRelaxed(const rows::CalendarVisit &visit) const {
    if (visit.carer_count() < 2) {
        return false;
    }

    const auto &nodes = solver_wrapper_.GetNodePair(visit);
    const auto first_index = index_manager_.NodeToIndex(nodes.first);
    const auto second_index = index_manager_.NodeToIndex(nodes.second);
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

    if (avoid_symmetry_) {
        if (first_vehicle > second_vehicle) {
            // symmetry violation
            return true;
        }

        CHECK_LT(first_vehicle, second_vehicle);
    }

    // different arrival time
    return solution_->Min(time_dim_->CumulVar(first_index)) !=
           solution_->Min(time_dim_->CumulVar(second_index));
}

bool rows::MultipleVisitQuery::IsSatisfied(const rows::CalendarVisit &visit) const {
    if (visit.carer_count() < 2) {
        return false;
    }


    const auto &nodes = solver_wrapper_.GetNodePair(visit);
    const auto first_index = index_manager_.NodeToIndex(nodes.first);
    const auto second_index = index_manager_.NodeToIndex(nodes.second);
    const auto first_vehicle = solution_->Min(model_.VehicleVar(first_index));
    const auto second_vehicle = solution_->Min(model_.VehicleVar(second_index));

    if (avoid_symmetry_) {
        if (first_vehicle > second_vehicle) {
            return false;
        }
    }

    return first_vehicle != -1 && second_vehicle != -1 && first_vehicle != second_vehicle
           && solution_->Min(time_dim_->CumulVar(first_index)) == solution_->Min(time_dim_->CumulVar(second_index));
}

void rows::MultipleVisitQuery::Print(std::shared_ptr<rows::Printer> printer) const {
    for (const auto &visit : this->solver_wrapper_.problem().visits()) {
        if (visit.carer_count() <= 1) {
            continue;
        }

        const auto &nodes = solver_wrapper_.GetNodes(visit);
        CHECK_EQ(nodes.size(), 2);

        auto node_it = std::begin(nodes);
        const auto first_visit_node = *node_it;
        const auto second_visit_node = *std::next(node_it);

        auto first_visit_index = index_manager_.NodeToIndex(first_visit_node);
        auto second_visit_index = index_manager_.NodeToIndex(second_visit_node);
        const auto first_vehicle = solution_->Min(model_.VehicleVar(first_visit_index));
        const auto second_vehicle = solution_->Min(model_.VehicleVar(second_visit_index));
        const auto first_time = solution_->Min(time_dim_->CumulVar(first_visit_index));
        const auto second_time = solution_->Min(time_dim_->CumulVar(second_visit_index));
        const auto status = (first_vehicle != -1
                             && second_vehicle != -1
                             && first_vehicle != second_vehicle
                             && first_time == second_time);

        printer->operator<<((boost::format(
                "Visit %3d %3d - [%2d %2d] [%2d %2d] - [%6d %6d] [%6d %6d] - [%2d %2d] - [%6d %6d] [%6d %6d] - %3d")
                             % first_visit_node
                             % second_visit_node
                             % solution_->Min(model_.VehicleVar(first_visit_index))
                             % solution_->Min(model_.VehicleVar(second_visit_index))
                             % solution_->Max(model_.VehicleVar(first_visit_index))
                             % solution_->Max(model_.VehicleVar(second_visit_index))
                             % solution_->Min(time_dim_->CumulVar(first_visit_index))
                             % solution_->Min(time_dim_->CumulVar(second_visit_index))
                             % solution_->Max(time_dim_->CumulVar(first_visit_index))
                             % solution_->Max(time_dim_->CumulVar(second_visit_index))
                             % solution_->Min(model_.ActiveVar(first_visit_index))
                             % solution_->Min(model_.ActiveVar(second_visit_index))
                             % solution_->Min(time_dim_->SlackVar(first_visit_index))
                             % solution_->Min(time_dim_->SlackVar(second_visit_index))
                             % solution_->Max(time_dim_->SlackVar(first_visit_index))
                             % solution_->Max(time_dim_->SlackVar(second_visit_index))
                             % status).str());
    }
}

void rows::MultipleVisitQuery::SetAssignment(operations_research::Assignment const *solution) {
    solution_ = solution;
}
