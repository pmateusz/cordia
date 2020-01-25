#include "delay_tracker.h"


rows::DelayTracker::DelayTracker(const rows::SolverWrapper &solver,
                                 const rows::History &history,
                                 const operations_research::RoutingDimension *dimension)
        : dimension_{dimension},
          model_{dimension_->model()},
          duration_sample_{solver, history, dimension_} {
    const auto num_indices = duration_sample_.num_indices();
    const auto num_samples = duration_sample_.size();
    records_.resize(num_indices);
    start_.resize(num_indices);
    delay_.resize(num_indices);
    for (auto index = 0; index < num_indices; ++index) {
        records_[index].index = index;

        int64 duration = 0;
        if (duration_sample_.is_visit(index)) {
            const auto node = solver.index_manager().IndexToNode(index);
            duration = solver.NodeToVisit(node).duration().total_seconds();
        }
        records_[index].duration = duration;

        start_[index].resize(num_samples, duration_sample_.start_min(index));
        delay_[index].resize(num_samples, 0);
    }
}

int64 rows::DelayTracker::GetMeanDelay(int64 node) const {
    const auto &delay = Delay(node);
    int64 total_delay = std::accumulate(std::cbegin(delay), std::cend(delay), static_cast<int64>(0));
    return static_cast<double>(total_delay) / static_cast<double>(delay.size());
}

int64 rows::DelayTracker::GetDelayProbability(int64 node) const {
    const auto &delay = Delay(node);
    const auto num_scenarios = delay.size();
    int64 delayed_arrival_count = 0;
    for (std::size_t scenario = 0; scenario < num_scenarios; ++scenario) {
        if (delay[scenario] > 0) { ++delayed_arrival_count; }
    }
    const double probability = static_cast<double>(delayed_arrival_count) * 100.0 / static_cast<double>(num_scenarios);
    return std::ceil(probability);
}

void rows::DelayTracker::UpdateAllPaths() {
    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        UpdatePathRecords(vehicle);
    }

    ComputeAllPathsDelay();
}

void rows::DelayTracker::UpdateAllPaths(operations_research::Assignment *assignment) {
    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        UpdatePathRecords(vehicle, assignment);
    }

    ComputeAllPathsDelay();
}

void rows::DelayTracker::ComputeAllPathsDelay() {
    const auto num_samples = duration_sample_.size();
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
    for (int64 index = 0; index < duration_sample_.num_indices(); ++index) {
        if (!duration_sample_.has_sibling(index)) {
            continue;
        }

        const auto sibling = duration_sample_.sibling(index);
        CHECK(start_.at(index) == start_.at(sibling));
    }

    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        ComputePathDelay(vehicle);
    }
}

void rows::DelayTracker::UpdatePath(int vehicle) {
    UpdatePathRecords(vehicle);

    const auto num_samples = duration_sample_.size();
    for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
        PropagateNode(model_->Start(vehicle), scenario);
    }

    ComputePathDelay(vehicle);
}

void rows::DelayTracker::UpdatePath(int vehicle, operations_research::Assignment *assignment) {
    UpdatePathRecords(vehicle, assignment);

    const auto num_samples = duration_sample_.size();
    for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
        PropagateNode(model_->Start(vehicle), scenario);
    }

    ComputePathDelay(vehicle);
}

void rows::DelayTracker::UpdatePathRecords(int vehicle) {
    int64 current_index = model_->Start(vehicle);
    int64 next_index = model_->NextVar(current_index)->Value();
    if (current_index == next_index) {
        return;
    }

    const auto &break_intervals = dimension_->GetBreakIntervalsOfVehicle(vehicle);
    const auto num_breaks = break_intervals.size();

    int64 break_pos = 0;
    int64 current_time = 0;
    while (break_pos < num_breaks && break_intervals[break_pos]->StartMax() <= dimension_->CumulVar(current_index)->Min()) {
        current_time = std::max(current_time, break_intervals[break_pos]->StartMin()) + break_intervals[break_pos]->DurationMin();
        ++break_pos;
    }

    while (!model_->IsEnd(current_index)) {
//        CHECK_LE(current_time, dimension_->CumulVar(current_index)->Max()) << duration_sample_.start_max(current_index);
        current_time = std::max(current_time, dimension_->CumulVar(current_index)->Min()) + records_.at(current_index).duration +
                       records_.at(current_index).travel_time;

        std::fill(std::begin(start_.at(current_index)), std::end(start_.at(current_index)), duration_sample_.start_min(current_index));
        std::fill(std::begin(delay_.at(current_index)), std::end(delay_.at(current_index)), 0);

        next_index = model_->NextVar(current_index)->Value();

        int64 current_break_duration = 0;
        int64 last_break_min = 0;
        int64 last_break_duration = 0;
        CHECK_LT(break_pos, num_breaks);

        do {
            const auto next_visit_strictly_precedes_break = dimension_->CumulVar(next_index)->Max() <= break_intervals[break_pos]->StartMin();
            if (next_visit_strictly_precedes_break) {
                break;
            }

            const auto break_strictly_precedes_next_visit = break_intervals[break_pos]->StartMax() <= dimension_->CumulVar(next_index)->Min();
            if (!break_strictly_precedes_next_visit) {
                const auto time_after_break =
                        std::max(current_time, break_intervals[break_pos]->StartMin()) + break_intervals[break_pos]->DurationMin();
                const auto time_after_next_visit =
                        std::max(current_time, dimension_->CumulVar(next_index)->Min()) + records_.at(next_index).duration +
                        records_.at(next_index).travel_time;
                const auto break_weakly_precedes_next_visit = (time_after_break <= dimension_->CumulVar(next_index)->Min()) ||
                                                              (break_intervals[break_pos]->StartMax() <= time_after_next_visit);
                const auto next_visit_weakly_precedes_break = (time_after_next_visit <= break_intervals[break_pos]->StartMin()) ||
                                                              (dimension_->CumulVar(next_index)->Max() <=
                                                               time_after_break); // performing a visit does not affect break or visit cannot be performed after break

                if (next_visit_weakly_precedes_break) {
                    break;
                }

                if (!break_weakly_precedes_next_visit) {
                    // regardless of the waiting time we prefer doing a visit before break if both options are possible

//                    break;

                    const auto break_before_next_visit_waiting_time = std::max(0l, break_intervals[break_pos]->StartMin() - current_time)
                                                                      + std::max(0l, dimension_->CumulVar(next_index)->Min() - time_after_break);
                    const auto next_visit_before_break_waiting_time = std::max(0l, dimension_->CumulVar(next_index)->Min() - current_time) +
                                                                      std::max(0l, time_after_next_visit - break_intervals[break_pos]->StartMin());

//                    // if can do visit without waiting then do the visit
                    if (current_time >= dimension_->CumulVar(next_index)->Min()) {
                        break;
                    }

                    if (next_visit_before_break_waiting_time <= break_before_next_visit_waiting_time) {
                        break;
                    }
                }
            }

            current_time = std::max(current_time, break_intervals[break_pos]->StartMin()) + break_intervals[break_pos]->DurationMin();
            last_break_min = break_intervals[break_pos]->StartMin();
            current_break_duration += break_intervals[break_pos]->DurationMin();
            last_break_duration = break_intervals[break_pos]->DurationMin();
            ++break_pos;
        } while (break_pos < num_breaks);

        auto &record = records_.at(current_index);
        record.next = next_index;
        record.travel_time = model_->GetArcCostForVehicle(current_index, next_index, vehicle);
        record.break_min = last_break_min + last_break_duration - current_break_duration;
        record.break_duration = current_break_duration;

        current_index = next_index;
    }

    std::fill(std::begin(start_.at(current_index)), std::end(start_.at(current_index)), duration_sample_.start_min(current_index));
    std::fill(std::begin(delay_.at(current_index)), std::end(delay_.at(current_index)), 0);

    // either iterated through all breaks or finished before the last break
    CHECK(break_pos == num_breaks
          || dimension_->CumulVar(current_index)->Min() <= break_intervals[break_pos]->StartMin()
          || break_intervals[break_pos]->StartMin() + break_intervals[break_pos]->DurationMin() <= dimension_->CumulVar(current_index)->Max());
}

void rows::DelayTracker::UpdatePathRecords(int vehicle, operations_research::Assignment *assignment) {
    int64 current_index = model_->Start(vehicle);
    int64 next_index = assignment->Value(model_->NextVar(current_index));
    if (current_index == next_index) {
        return;
    }

    const auto &break_intervals = dimension_->GetBreakIntervalsOfVehicle(vehicle);
    const auto num_breaks = break_intervals.size();

    int64 break_pos = 0;
    while (break_pos < num_breaks &&
           assignment->StartMin(break_intervals[break_pos]) + assignment->DurationMin(break_intervals[break_pos])
           <= assignment->Min(dimension_->CumulVar(current_index))) {
        ++break_pos;
    }

    while (!model_->IsEnd(current_index)) {
        std::fill(std::begin(start_.at(current_index)), std::end(start_.at(current_index)), duration_sample_.start_min(current_index));
        std::fill(std::begin(delay_.at(current_index)), std::end(delay_.at(current_index)), 0);

        next_index = assignment->Value(model_->NextVar(current_index));


        int64 current_break_duration = 0;
        int64 last_break_min = 0;
        int64 last_break_duration = 0;
        while (break_pos < num_breaks
               && ((assignment->StartMin(break_intervals[break_pos]) + assignment->DurationMin(break_intervals[break_pos]) <
                    assignment->Min(dimension_->CumulVar(next_index)))
                   || (assignment->StartMax(break_intervals[break_pos]) < assignment->Min(dimension_->CumulVar(next_index))))) {
            last_break_min = assignment->StartMin(break_intervals[break_pos]);
            last_break_duration = assignment->DurationMin(break_intervals[break_pos]);
            current_break_duration += assignment->DurationMin(break_intervals[break_pos]);
            ++break_pos;
        }

        auto &record = records_.at(current_index);
        record.next = next_index;
        record.travel_time = model_->GetArcCostForVehicle(current_index, next_index, vehicle);
        record.break_min = last_break_min + last_break_duration - current_break_duration;
        record.break_duration = current_break_duration;

        if (next_index == 39 || current_index == 39) {
            LogNodeDetails(vehicle, current_index, assignment);
            LOG(INFO) << "Break Position: " << break_pos;
        }

        current_index = next_index;
    }

    std::fill(std::begin(start_.at(current_index)), std::end(start_.at(current_index)), duration_sample_.start_min(current_index));
    std::fill(std::begin(delay_.at(current_index)), std::end(delay_.at(current_index)), 0);

    // either iterated through all breaks or finished before the last break
    CHECK(break_pos == num_breaks
          || assignment->Min(dimension_->CumulVar(current_index)) <= assignment->StartMin(break_intervals[break_pos])
          || assignment->StartMin(break_intervals[break_pos]) + assignment->DurationMin(break_intervals[break_pos]) <=
             assignment->Max(dimension_->CumulVar(current_index)));
}

void rows::DelayTracker::LogNodeDetails(int vehicle, int64 node) {
    std::stringstream msg;

    msg << "Node " << node << " details:" << std::endl;
    msg << "Start time: [" << dimension_->CumulVar(node)->Min() << ", " << dimension_->CumulVar(node)->Max() << "]" << std::endl;
    msg << "Duration: ?" << std::endl;

    msg << "Record " << node << " details: " << std::endl;
    msg << "Travel time: " << records_.at(node).travel_time << std::endl;
    msg << "Break duration: " << records_.at(node).break_duration << std::endl;
    msg << "Break min: " << records_.at(node).break_min << std::endl;

    for (const auto &break_interval : dimension_->GetBreakIntervalsOfVehicle(vehicle)) {
        msg << "Break: [" << break_interval->StartMin() << ", " << break_interval->StartMax() << "] " << break_interval->DurationMin() << std::endl;
    }

    LOG(INFO) << msg.str();
}

void rows::DelayTracker::LogNodeDetails(int vehicle, int64 node, operations_research::Assignment *assignment) {
    std::stringstream msg;

    msg << "Node " << node << " details:" << std::endl;
    msg << "Vehicle: " << vehicle << std::endl;
    msg << "Start time: [" << assignment->Min(dimension_->CumulVar(node)) << ", " << assignment->Max(dimension_->CumulVar(node)) << "]" << std::endl;
    msg << "Duration: ?" << std::endl;

    msg << "Record " << node << " details: " << std::endl;
    msg << "Travel time: " << records_.at(node).travel_time << std::endl;
    msg << "Break duration: " << records_.at(node).break_duration << std::endl;
    msg << "Break min: " << records_.at(node).break_min << std::endl;

    for (const auto &break_interval : dimension_->GetBreakIntervalsOfVehicle(vehicle)) {
        msg << "Break: [" << assignment->StartMin(break_interval) << ", " << assignment->StartMax(break_interval) << "] "
            << assignment->DurationMin(break_interval) << std::endl;
    }

    LOG(INFO) << msg.str();
}

void rows::DelayTracker::ComputePathDelay(int vehicle) {
    const auto num_samples = duration_sample_.size();

    int64 current_index = records_.at(model_->Start(vehicle)).next;
    while (!model_->IsEnd(current_index)) {
        for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
            delay_.at(current_index).at(scenario) = start_.at(current_index).at(scenario) - duration_sample_.start_max(current_index);
        }
        current_index = records_.at(current_index).next;
    }
}

void rows::DelayTracker::PropagateNode(int64 index, std::size_t scenario) {
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

void rows::DelayTracker::PropagateNodeWithSiblings(int64 index, std::size_t scenario, std::unordered_set<int64> &siblings_updated) {
    auto current_index = index;
    while (!model_->IsEnd(current_index)) {
        const auto &current_record = records_.at(current_index);
        const auto arrival_time = GetArrivalTime(current_record, scenario);

        if (start_.at(current_record.next).at(scenario) < arrival_time) {
            start_.at(current_record.next).at(scenario) = arrival_time;
            if (duration_sample_.has_sibling(current_record.next)) {
                const auto sibling = duration_sample_.sibling(current_record.next);
                if (start_.at(sibling).at(scenario) < arrival_time) {
                    start_.at(sibling).at(scenario) = arrival_time;
                    siblings_updated.emplace(sibling);
                }
            }
        }

        current_index = current_record.next;
    }
}

int64 rows::DelayTracker::GetArrivalTime(const rows::DelayTracker::TrackRecord &record, std::size_t scenario) const {
    auto arrival_time = start_.at(record.index).at(scenario) + duration_sample_.duration(record.index, scenario) + record.travel_time;
    if (arrival_time > record.break_min) {
        arrival_time += record.break_duration;
    } else {
        arrival_time = std::max(arrival_time, record.break_min + record.break_duration);
    }
    return arrival_time;
}
