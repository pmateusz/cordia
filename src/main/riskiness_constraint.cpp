#include "riskiness_constraint.h"

rows::RiskinessConstraint::RiskinessConstraint(operations_research::IntVar *riskiness_index,
                                               const operations_research::RoutingDimension *dimension,
                                               std::shared_ptr<const DurationSample> duration_sample)
        : Constraint(dimension->model()->solver()),
          riskiness_index_{riskiness_index},
          model_{dimension->model()},
          dimension_{dimension},
          duration_sample_{std::move(duration_sample)} {
    completed_paths_.resize(model_->vehicles());
    vehicle_demons_.resize(model_->vehicles());

    const auto num_indices = duration_sample_->num_indices();
    const auto num_samples = duration_sample_->size();
    records_.resize(num_indices);
    start_.resize(num_indices);
    delay_.resize(num_indices);
    for (auto index = 0; index < num_indices; ++index) {
        records_[index].index = index;
        start_[index].resize(num_samples, duration_sample_->start_min(index));
        delay_[index].resize(num_samples, 0);
    }
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

    int64 break_pos = 0;
    while (break_pos < num_breaks && break_start_min[break_pos] + break_duration[break_pos] <= dimension_->CumulVar(current_index)->Min()) {
        ++break_pos;
    }

    std::vector<int64> path{current_index};
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

        auto &record = records_.at(current_index);
        record.next = next_index;
        record.travel_time = model_->GetArcCostForVehicle(current_index, next_index, vehicle);
        record.break_min = last_break_min + last_break_duration - current_break_duration;
        record.break_duration = current_break_duration;

        current_index = next_index;
        path.emplace_back(current_index);
    }

    // either iterated through all breaks or finished before the last break
    CHECK(break_pos == num_breaks || dimension_->CumulVar(current_index)->Min() <= break_start_min[break_pos]);

    ComputeDelay({model_->Start(vehicle)});

    for (const auto index : path) {
        if (duration_sample_->is_visit(index)) {
            const auto max_delay = MaxDelay(index);
            if (max_delay > 0) {
                const int64 mean_delay = MeanDelay(index);
                if (mean_delay > riskiness_index_->Min()) {
                    solver()->AddConstraint(solver()->MakeGreaterOrEqual(riskiness_index_, mean_delay));
                }
            }
        }
    }

    if (riskiness_index_->Min() < 0) {
        solver()->AddConstraint(solver()->MakeGreaterOrEqual(riskiness_index_, 1));
    }
}

void rows::RiskinessConstraint::ResetNode(int64 index) {
    std::fill(std::begin(start_.at(index)), std::end(start_.at(index)), duration_sample_->start_min(index));
    std::fill(std::begin(delay_.at(index)), std::end(delay_.at(index)), 0);
}

void rows::RiskinessConstraint::PropagateNode(int64 index, std::size_t scenario) {
    auto current_index = index;
    while (!model_->IsEnd(current_index)) {
        const auto &current_record = records_.at(current_index);
        auto next_index = current_record.next;

        auto arrival_time = start_.at(current_index).at(scenario) + duration_sample_->duration(current_index, scenario) + current_record.travel_time;
        if (arrival_time > current_record.break_min) {
            arrival_time += current_record.break_duration;
        } else {
            arrival_time = std::max(arrival_time, current_record.break_min + current_record.break_duration);
        }
        start_.at(next_index).at(scenario) = std::max(arrival_time, start_.at(next_index).at(scenario));

        current_index = next_index;
    }
}

// TODO: compute delay synchronizing nodes
void rows::RiskinessConstraint::ComputeDelay(const std::vector<int64> &start_nodes) {
    for (const auto start_node : start_nodes) {
        auto current_index = start_node;
        while (!model_->IsEnd(current_index)) {
            ResetNode(current_index);
            current_index = records_.at(current_index).next;
        }
    }

    const auto num_samples = duration_sample_->size();
    for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
        for (const auto start_node : start_nodes) {
            PropagateNode(start_node, scenario);
        }
    }

    const auto num_indices = start_.size();
    for (int64 index = 0; index < num_indices; ++index) {
        for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
            delay_.at(index).at(scenario) = start_.at(index).at(scenario) - duration_sample_->start_max(index);
        }
    }
}

int64 rows::RiskinessConstraint::MaxDelay(int64 index) const {
    return *std::max_element(std::cbegin(delay_.at(index)), std::cend(delay_.at(index)));
}

int64 rows::RiskinessConstraint::MeanDelay(int64 index) const {
    const auto accumulated_value = std::accumulate(std::cbegin(delay_.at(index)), std::cend(delay_.at(index)), 0l);
    return accumulated_value / static_cast<int64>(delay_.at(index).size());
}
