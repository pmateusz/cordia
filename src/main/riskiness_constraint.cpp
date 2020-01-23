#include "riskiness_constraint.h"

rows::RiskinessConstraint::RiskinessConstraint(operations_research::IntVar *riskiness_index,
                                               const operations_research::RoutingDimension *dimension,
                                               std::shared_ptr<DurationSample> duration_sample)
        : Constraint(dimension->model()->solver()),
          riskiness_index_{riskiness_index},
          model_{dimension->model()},
          dimension_{dimension},
          duration_sample_{std::move(duration_sample)} {
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

struct NodeRecord {
    int64 next;
    int64 travel_time;
    int64 break_min;
    int64 break_duration;
};

void rows::RiskinessConstraint::PropagateVehicle(int vehicle) {
    if (completed_paths_[vehicle]->Max() == 0) {
        return;
    }

    // for each scenario
    // for each node keep min, max, travel_time, next, opt-sibling, break_min, break_duration
    int64 current_index = model_->Start(vehicle);
    int64 next_index = model_->NextVar(current_index)->Value();
    if (current_index == next_index) {
        return;
    }

    const auto &break_intervals = dimension_->GetBreakIntervalsOfVehicle(vehicle);
    const auto num_breaks = break_intervals.size();
    std::vector<int64> break_start_min;
    break_start_min.resize(num_breaks);

    std::vector<int64> break_duration;
    break_duration.resize(num_breaks);

    for (const auto &interval : break_intervals) {
        break_start_min.emplace_back(interval->StartMin());
        break_duration.emplace_back(interval->DurationMin());
    }

    std::vector<NodeRecord> records;
    records.resize(model_->nodes());

    int64 break_pos = 0;
    while (break_pos < num_breaks && break_start_min[break_pos] + break_duration[break_pos] <= dimension_->CumulVar(current_index)->Min()) {
        ++break_pos;
    }

    current_index = next_index;
    while (!model_->IsEnd(current_index)) {
        next_index = model_->NextVar(current_index)->Value();

        int64 current_break_duration = 0;
        int64 last_break_min = 0;
        int64 last_break_duration = 0;
        while (break_pos < num_breaks && break_start_min[break_pos] < dimension_->CumulVar(next_index)->Min()) {
            last_break_min = break_start_min[break_pos];
            last_break_duration = break_duration[break_pos];
            current_break_duration += break_duration[break_pos];
            ++break_pos;
        }

        auto &record = records.at(current_index);
        record.next = next_index;
        record.travel_time = model_->GetArcCostForVehicle(current_index, next_index, vehicle);
        record.break_min = last_break_min + last_break_duration - current_break_duration;
        record.break_duration = current_break_duration;

        current_index = next_index;
    }

    // either iterated through all breaks or finished before the last break
    CHECK(break_pos == num_breaks || dimension_->CumulVar(current_index)->Min() <= break_start_min[break_pos]);

    solver()->AddConstraint(solver()->MakeGreaterOrEqual(riskiness_index_, 1000));
}
