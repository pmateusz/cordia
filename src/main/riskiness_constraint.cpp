#include "riskiness_constraint.h"

rows::RiskinessConstraint::RiskinessConstraint(operations_research::IntVar *riskiness_index, const operations_research::RoutingDimension *dimension)
        : Constraint(dimension->model()->solver()),
          riskiness_index_{riskiness_index},
          model_{dimension->model()},
          dimension_{dimension} {
    completed_paths_.resize(model_->vehicles());
    vehicle_demons_.resize(model_->vehicles());
}

void rows::RiskinessConstraint::Post() {
    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        vehicle_demons_[vehicle] = MakeDelayedConstraintDemon1(
                solver(), this, &RiskinessConstraint::PropagateVehicle,
                "PropagateVehicle", vehicle);

        completed_paths_[vehicle] = solver()->MakeBoolVar();

        const auto path_connected_constraint
                = solver()->MakePathConnected(model_->Nexts(),
                                              {model_->Start(vehicle)},
                                              {model_->End(vehicle)},
                                              {completed_paths_[vehicle]});

        solver()->AddConstraint(path_connected_constraint);
        completed_paths_[vehicle]->WhenBound(vehicle_demons_[vehicle]);
    }

    std::vector<operations_research::IntVar *> dropped_visits;
    for (int node = 0; node < model_->nodes(); ++node) {
        dropped_visits.push_back(solver()->MakeIsEqualCstVar(model_->VehicleVar(node), -1)->Var());
    }
    solver()->AddConstraint(
            solver()->MakeGreaterOrEqual(riskiness_index_, solver()->MakeProd(solver()->MakeSum(dropped_visits), 1000)));
}

void rows::RiskinessConstraint::InitialPropagate() {
    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        if (completed_paths_[vehicle]->Bound()) {
            PropagateVehicle(vehicle);
        }
    }
}

void rows::RiskinessConstraint::PropagateVehicle(int vehicle) {
    if (completed_paths_[vehicle]->Max() == 0) {
        return;
    }

    // for each scenario
    // for each node keep min, max, transit, next, opt-sibling, break_min, break_duration

    int64 current_index = model_->Start(vehicle);
    std::vector<int64> path{current_index};
    while (!model_->IsEnd(current_index)) {
        const auto next_index = model_->NextVar(current_index)->Value();

        current_index = next_index;
        path.emplace_back(current_index);
    }

    const auto &break_intervals = dimension_->GetBreakIntervalsOfVehicle(vehicle);
    for (const auto &interval : break_intervals) {
        interval->StartMin();
        interval->DurationMin();
    }
}
