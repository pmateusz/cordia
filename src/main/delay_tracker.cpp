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
    explicit AssignmentData(const operations_research::Assignment *assignment)
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
        : solver_{solver},
          dimension_{dimension},
          model_{dimension_->model()},
          duration_sample_{solver, history, dimension_} {
    const auto num_indices = duration_sample_.num_indices();
    const auto num_samples = duration_sample_.size();
    records_.resize(num_indices);
    start_.resize(num_indices);
    delay_.resize(num_indices);
    for (auto index = 0; index < num_indices; ++index) {
        records_[index].index = index;
        records_[index].next = -1;

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

std::vector<int64> rows::DelayTracker::BuildPath(int vehicle, operations_research::Assignment const *assignment) {
    const AssignmentData assignment_data{assignment};
    return BuildPathFromSource<decltype(assignment_data)>(vehicle, assignment_data);
}

void rows::DelayTracker::UpdateAllPaths() {
    static const SolverData DATA;

    UpdateAllPathsFromSource<decltype(DATA)>(DATA);
}

void rows::DelayTracker::UpdateAllPaths(operations_research::Assignment const *assignment) {
    const AssignmentData assignment_data{assignment};

    UpdateAllPathsFromSource<decltype(assignment_data)>(assignment_data);
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

    int64 current_index = records_[model_->Start(vehicle)].next;
    while (!model_->IsEnd(current_index)) {
        for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
            delay_[current_index][scenario] = start_[current_index][scenario] - duration_sample_.start_max(current_index);
        }
        current_index = records_[current_index].next;
    }
}

int64 rows::DelayTracker::GetArrivalTimeWithBreak(const rows::DelayTracker::TrackRecord &record, std::size_t scenario) const {
    CHECK_NE(record.next, -1);

    auto arrival_time = start_[record.index][scenario] + duration_sample_.duration(record.index, scenario) + record.travel_time;
    if (arrival_time > record.break_min) {
        arrival_time += record.break_duration;
    } else {
        arrival_time = record.break_min + record.break_duration;
    }

    if (arrival_time < start_[record.next][scenario]) {
        arrival_time = start_[record.next][scenario];
    }

    return arrival_time;
}

int64 rows::DelayTracker::GetArrivalTimeNoBreak(const rows::DelayTracker::TrackRecord &record, std::size_t scenario) const {
    CHECK_NE(record.next, -1);

    auto arrival_time = start_[record.index][scenario] + duration_sample_.duration(record.index, scenario) + record.travel_time;
    if (arrival_time < start_[record.next][scenario]) {
        arrival_time = start_[record.next][scenario];
    }

    return arrival_time;
}

rows::DelayTracker::PartialPath const *rows::DelayTracker::SelectBestPath(const PartialPath &left, const PartialPath &right) const {
    CHECK(left.IsComplete());
    CHECK(right.IsComplete());
    CHECK_EQ(left.slack.size(), right.slack.size());

    const auto num_slack = left.slack.size();
    std::vector<int64> slack_diff;
    slack_diff.resize(num_slack);

    for (std::size_t pos = 0; pos < num_slack; ++pos) {
        slack_diff[pos] = std::min(left.slack[pos], 3600l) - std::min(right.slack[pos], 3600l);
    }
    auto total_budget = std::accumulate(std::cbegin(slack_diff), std::cend(slack_diff), 0l);

    if (total_budget > 0) {
        return &left;
    } else if (total_budget < 0) {
        return &right;
    }

    for (std::size_t pos = 0; pos < num_slack; ++pos) {
        slack_diff[pos] = left.slack[pos] - right.slack[pos];
    }
    total_budget = std::accumulate(std::cbegin(slack_diff), std::cend(slack_diff), 0l);

//        int64 candidate_slack = paths[candidate_pos].TotalNormalizedSlack();
//        if (result_slack < candidate_slack) {
//            result_pos = candidate_pos;
//            result_slack = candidate_slack;
//        }

    if (total_budget > 0) {
        return &left;
    } else if (total_budget < 0) {
        return &right;
    }
    return &left;
}

rows::DelayTracker::PartialPath const *rows::DelayTracker::SelectBestPath(const std::vector<PartialPath> &paths) const {
    const auto num_paths = paths.size();
    std::size_t result_pos = 0;
    for (; result_pos < num_paths && !paths[result_pos].IsComplete(); ++result_pos);
    if (result_pos == num_paths) { return nullptr; }

    rows::DelayTracker::PartialPath const *result_path = &paths[result_pos];
    for (std::size_t candidate_pos = result_pos + 1; candidate_pos < num_paths; ++candidate_pos) {
        if (!paths[candidate_pos].IsComplete()) {
            continue;
        }

        result_path = SelectBestPath(*result_path, paths[candidate_pos]);
    }

    return result_path;
}
