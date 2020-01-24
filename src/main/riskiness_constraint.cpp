#include "riskiness_constraint.h"

#include <chrono>

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
                solver(), this, &RiskinessConstraint::PropagatePath,
                "PropagateVehicle", vehicle);

        completed_paths_[vehicle] = solver()->MakeBoolVar();

        const auto path_connected_constraint
                = solver()->MakePathConnected(model_->Nexts(),
                                              {model_->Start(vehicle)},
                                              {model_->End(vehicle)},
                                              {completed_paths_[vehicle]});

        solver()->AddConstraint(path_connected_constraint);
//        completed_paths_[vehicle]->WhenBound(vehicle_demons_[vehicle]);
    }


    all_paths_completed_ = solver()->MakeIsEqualCstVar(solver()->MakeSum(completed_paths_), completed_paths_.size());
    auto all_paths_completed_demon = MakeDelayedConstraintDemon0(solver(), this, &RiskinessConstraint::PropagateFull, "PropagateFull");
    all_paths_completed_->WhenBound(all_paths_completed_demon);
}

void rows::RiskinessConstraint::InitialPropagate() {
    auto has_incomplete_paths = false;
    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        if (!completed_paths_[vehicle]->Bound()) {
            has_incomplete_paths = true;
            break;
        }
    }

    if (has_incomplete_paths) {
        for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
            if (completed_paths_[vehicle]->Bound()) {
                PropagatePath(vehicle);
            }
        }
    } else {
        PropagateFull();
    }
}

void rows::RiskinessConstraint::PropagatePath(int vehicle) {
    if (completed_paths_[vehicle]->Max() == 0) {
        return;
    }

    UpdatePath(vehicle);

    const auto num_samples = duration_sample_->size();
    for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
        PropagateNode(model_->Start(vehicle), scenario);
    }

    ComputePathDelay(vehicle);
    PostPathConstraints(vehicle);
}

void rows::RiskinessConstraint::PropagateFull() {
    if (all_paths_completed_->Min() == 0) {
        return;
    }

    const auto start = std::chrono::high_resolution_clock::now();

    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        UpdatePath(vehicle);
    }

    const auto num_samples = duration_sample_->size();
    for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
        std::unordered_set<int64> siblings_updated;
        for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
            PropagateNodeWithSiblings(model_->Start(vehicle), scenario, siblings_updated);
        }

        while (!siblings_updated.empty()) {
            const auto current_node = *siblings_updated.begin();
            siblings_updated.erase(siblings_updated.begin());

            PropagateNodeWithSiblings(current_node, scenario, siblings_updated);
        }
    }

    // TODO: remove check
//    for (int64 index = 0; index < duration_sample_->num_indices(); ++index) {
//        if (!duration_sample_->has_sibling(index)) {
//            continue;
//        }
//
//        const auto sibling = duration_sample_->sibling(index);
//        CHECK(start_.at(index) == start_.at(sibling));
//    }

    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        ComputePathDelay(vehicle);
        PostPathConstraints(vehicle);
    }

    const auto end = std::chrono::high_resolution_clock::now();
    LOG(INFO) << "FullPropagation in  " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms: ("
              << riskiness_index_->Min() << ", " << riskiness_index_->Max() << ")";
}

void rows::RiskinessConstraint::UpdatePath(int vehicle) {
    int64 current_index = model_->Start(vehicle);
    int64 next_index = model_->NextVar(current_index)->Value();
    if (current_index == next_index) {
        return;
    }

    const auto &break_intervals = dimension_->GetBreakIntervalsOfVehicle(vehicle);
    const auto num_breaks = break_intervals.size();

    int64 break_pos = 0;
    while (break_pos < num_breaks &&
           break_intervals[break_pos]->StartMin() + break_intervals[break_pos]->DurationMin() <= dimension_->CumulVar(current_index)->Min()) {
        ++break_pos;
    }

    while (!model_->IsEnd(current_index)) {
        std::fill(std::begin(start_.at(current_index)), std::end(start_.at(current_index)), duration_sample_->start_min(current_index));
        std::fill(std::begin(delay_.at(current_index)), std::end(delay_.at(current_index)), 0);

        next_index = model_->NextVar(current_index)->Value();

        int64 current_break_duration = 0;
        int64 last_break_min = 0;
        int64 last_break_duration = 0;
        while (break_pos < num_breaks && break_intervals[break_pos]->StartMin() < dimension_->CumulVar(next_index)->Min()) {
            last_break_min = break_intervals[break_pos]->StartMin();
            last_break_duration = break_intervals[break_pos]->DurationMin();
            current_break_duration += break_intervals[break_pos]->DurationMin();
            ++break_pos;
        }

        auto &record = records_.at(current_index);
        record.next = next_index;
        record.travel_time = model_->GetArcCostForVehicle(current_index, next_index, vehicle);
        record.break_min = last_break_min + last_break_duration - current_break_duration;
        record.break_duration = current_break_duration;

        current_index = next_index;
    }
    std::fill(std::begin(start_.at(current_index)), std::end(start_.at(current_index)), duration_sample_->start_min(current_index));
    std::fill(std::begin(delay_.at(current_index)), std::end(delay_.at(current_index)), 0);

    // either iterated through all breaks or finished before the last break
    CHECK(break_pos == num_breaks || dimension_->CumulVar(current_index)->Min() <= break_intervals[break_pos]->StartMin());
}

void rows::RiskinessConstraint::ComputePathDelay(int vehicle) {
    const auto num_indices = start_.size();
    const auto num_samples = duration_sample_->size();

    for (int64 index = 0; index < num_indices; ++index) {
        for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
            delay_.at(index).at(scenario) = start_.at(index).at(scenario) - duration_sample_->start_max(index);
        }
    }
}

void rows::RiskinessConstraint::PostPathConstraints(int vehicle) {
    auto current_index = model_->Start(vehicle);
    while (!model_->IsEnd(current_index)) {
        if (duration_sample_->is_visit(current_index)) {
            const auto max_delay = MaxDelay(current_index);
            if (max_delay > 0) {
                const int64 essential_riskiness = GetEssentialRiskiness(current_index);
                if (essential_riskiness > riskiness_index_->Min()) {
                    solver()->AddConstraint(solver()->MakeGreaterOrEqual(riskiness_index_, essential_riskiness));
                }
            }
        }
        current_index = records_.at(current_index).next;
    }
}

void rows::RiskinessConstraint::PropagateNode(int64 index, std::size_t scenario) {
    auto current_index = index;
    while (!model_->IsEnd(current_index)) {
        const auto &current_record = records_.at(current_index);
        const auto arrival_time = GetArrivalTime(current_record, scenario);

        if (start_.at(current_record.next).at(scenario) < arrival_time) {
            start_.at(current_record.next).at(scenario) = arrival_time;
        }

        current_index = current_record.next;
    }
}

void rows::RiskinessConstraint::PropagateNodeWithSiblings(int64 index, std::size_t scenario, std::unordered_set<int64> &siblings_updated) {
    auto current_index = index;
    while (!model_->IsEnd(current_index)) {
        const auto &current_record = records_.at(current_index);
        const auto arrival_time = GetArrivalTime(current_record, scenario);

        if (start_.at(current_record.next).at(scenario) < arrival_time) {
            start_.at(current_record.next).at(scenario) = arrival_time;
            if (duration_sample_->has_sibling(current_record.next)) {
                const auto sibling = duration_sample_->sibling(current_record.next);
                if (start_.at(sibling).at(scenario) < arrival_time) {
                    start_.at(sibling).at(scenario) = arrival_time;
                    siblings_updated.emplace(sibling);
                }
            }
        }

        current_index = current_record.next;
    }
}

int64 rows::RiskinessConstraint::GetArrivalTime(const rows::RiskinessConstraint::TrackRecord &record, std::size_t scenario) const {
    auto arrival_time = start_.at(record.index).at(scenario) + duration_sample_->duration(record.index, scenario) + record.travel_time;
    if (arrival_time > record.break_min) {
        arrival_time += record.break_duration;
    } else {
        arrival_time = std::max(arrival_time, record.break_min + record.break_duration);
    }
    return arrival_time;
}

int64 rows::RiskinessConstraint::MaxDelay(int64 index) const {
    return *std::max_element(std::cbegin(delay_.at(index)), std::cend(delay_.at(index)));
}

int64 rows::RiskinessConstraint::MeanDelay(int64 index) const {
    const auto accumulated_value = std::accumulate(std::cbegin(delay_.at(index)), std::cend(delay_.at(index)), 0l);
    return accumulated_value / static_cast<int64>(delay_.at(index).size());
}

int64 rows::RiskinessConstraint::GetEssentialRiskiness(int64 index) const {
    std::vector<int64> delays = delay_.at(index);
    std::sort(std::begin(delays), std::end(delays));

    // if last element is negative then index is zero
    const auto num_delays = delays.size();
    int64 delay_pos = num_delays - 1;
    if (delays.at(delay_pos) <= 0) {
        return 0;
    }

    if (delays.at(0) >= 0) {
        return kint64max;
    }

    // compute total delay
    int64 total_delay = 0;
    for (; delay_pos >= 0 && delays.at(delay_pos) >= 0; --delay_pos) {
        total_delay += delays.at(delay_pos);
    }
    CHECK_GT(total_delay, 0);
    CHECK_GT(delay_pos, 0);

    // return when not possible to increase the riskiness index
    if ((delay_pos + 1) * riskiness_index_->Min() >= total_delay) {
        return riskiness_index_->Min();
    }

    // find minimum traffic index that compensates the total delay
    int64 delay_budget = 0;
    for (; delay_pos > 0 && delay_budget + (delay_pos + 1) * delays.at(delay_pos) + total_delay > 0; --delay_pos) {
        delay_budget += delays.at(delay_pos);
    }

    int64 delay_balance = delay_budget + (delay_pos + 1) * delays.at(delay_pos) + total_delay;
    if (delay_balance < 0) {
        int64 riskiness_index = std::min(0l, delays.at(delay_pos + 1));
        CHECK_LE(riskiness_index, 0);

        int64 remaining_balance = total_delay + delay_budget + (delay_pos + 1) * riskiness_index;
        CHECK_GE(remaining_balance, 0);

        riskiness_index -= std::ceil(static_cast<double>(remaining_balance) / static_cast<double>(delay_pos + 1));
        CHECK_LE(riskiness_index * (delay_pos + 1) + delay_budget + total_delay, 0);

        return -riskiness_index;
    } else if (delay_balance > 0) {
        return kint64max;
    }

    return delays.at(delay_pos);
}
