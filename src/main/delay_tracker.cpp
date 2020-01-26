#include "delay_tracker.h"

class SolverData {
public:
    inline int64 Max(const operations_research::IntVar *variable) const { return variable->Max(); }

    inline int64 Min(const operations_research::IntVar *variable) const { return variable->Min(); }

    inline int64 Value(const operations_research::IntVar *variable) const { return variable->Value(); }

    inline int64 StartMax(const operations_research::IntervalVar *variable) const { return variable->StartMax(); }

    inline int64 StartMin(const operations_research::IntervalVar *variable) const { return variable->StartMin(); }

    inline int64 DurationMin(const operations_research::IntervalVar *variable) const { return variable->DurationMin(); }
};

class AssignmentData {
public:
    AssignmentData(const operations_research::Assignment *assignment)
            : assignment_{assignment} {}

    inline int64 Max(const operations_research::IntVar *variable) const { return assignment_->Max(variable); }

    inline int64 Min(const operations_research::IntVar *variable) const { return assignment_->Min(variable); }

    inline int64 Value(const operations_research::IntVar *variable) const { return assignment_->Value(variable); }

    inline int64 StartMax(const operations_research::IntervalVar *variable) const { return assignment_->StartMax(variable); }

    inline int64 StartMin(const operations_research::IntervalVar *variable) const { return assignment_->StartMin(variable); }

    inline int64 DurationMin(const operations_research::IntervalVar *variable) const { return assignment_->DurationMin(variable); }

private:
    operations_research::Assignment const *assignment_;
};

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
    static const SolverData DATA;

    UpdateAllPathsFromSource<decltype(DATA)>(DATA);
}

void rows::DelayTracker::UpdateAllPaths(operations_research::Assignment const *assignment) {
    const AssignmentData assignment_data{assignment};

    UpdateAllPathsFromSource<decltype(assignment_data)>(assignment_data);
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

// // expensive check
//    for (int64 index = 0; index < duration_sample_.num_indices(); ++index) {
//        if (!duration_sample_.has_sibling(index)) {
//            continue;
//        }
//
//        const auto sibling = duration_sample_.sibling(index);
//        CHECK(start_.at(index) == start_.at(sibling));
//    }

    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
        ComputePathDelay(vehicle);
    }
}

void rows::DelayTracker::UpdatePath(int vehicle) {
    static const SolverData SOLVER_DATA;

    UpdatePath<decltype(SOLVER_DATA)>(vehicle, SOLVER_DATA);
}

void rows::DelayTracker::UpdatePath(int vehicle, operations_research::Assignment const *assignment) {
    const AssignmentData assignment_data{assignment};

    UpdatePath<decltype(assignment_data)>(vehicle, assignment_data);
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
