#ifndef ROWS_DELAY_TRACKER_H
#define ROWS_DELAY_TRACKER_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "duration_sample.h"

// TODO: use access with brackets everywhere

namespace rows {

    class DelayTracker {
    public:
        struct TrackRecord {
            int64 index;
            int64 duration;
            int64 next;
            int64 travel_time;
            int64 break_min;
            int64 break_duration;
        };

        DelayTracker(const SolverWrapper &solver, const History &history, const operations_research::RoutingDimension *dimension);

        inline TrackRecord &Record(int64 node) { return records_.at(node); }

        inline const std::vector<int64> &Delay(int64 node) const { return delay_.at(node); }

        int64 GetMeanDelay(int64 node) const;

        int64 GetDelayProbability(int64 node) const;

        inline const operations_research::RoutingModel *model() const { return model_; }

        void UpdateAllPaths();

        void UpdateAllPaths(operations_research::Assignment const *assignment);

        void UpdatePath(int vehicle);

        void UpdatePath(int vehicle, operations_research::Assignment const *assignment);

    private:

        template<typename DataSource>
        void UpdateAllPathsFromSource(const DataSource &data) {
            for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
                const auto path = BuildPath<decltype(data)>(vehicle, data);
                UpdatePathRecords<decltype(data)>(vehicle, path, data);
            }

            ComputeAllPathsDelay();
        }

        template<typename DataSource>
        std::vector<int64> FastBuildPath(int vehicle, const DataSource &data) {
            // this function is heuristic and may return paths which violate start time of visit nodes and breaks

            int64 current_index = model_->Start(vehicle);
            int64 next_index = data.Value(model_->NextVar(current_index));
            if (current_index == next_index) {
                return {};
            }

            const auto &break_intervals = dimension_->GetBreakIntervalsOfVehicle(vehicle);
            const auto num_breaks = break_intervals.size();

            std::vector<int64> path;
            int64 break_pos = 0;
            int64 current_time = 0;
            while (break_pos < num_breaks && data.StartMax(break_intervals[break_pos]) <= data.Min(dimension_->CumulVar(current_index))) {
                current_time = std::max(current_time, data.StartMin(break_intervals[break_pos]) + data.DurationMin(break_intervals[break_pos]));

                if (break_pos > 0) {
                    path.emplace_back(-break_pos);
                }

                ++break_pos;
            }
            CHECK_GT(break_pos, 0);

            while (!model_->IsEnd(current_index)) {
//        CHECK_LE(current_time, dimension_->CumulVar(current_index)->Max()) << duration_sample_.start_max(current_index); // this check sometimes may fail in the fast method
                next_index = data.Value(model_->NextVar(current_index));

                current_time = std::max(current_time, data.Min(dimension_->CumulVar(current_index)))
                               + records_.at(current_index).duration
                               + model_->GetArcCostForVehicle(current_index, next_index, vehicle);
                path.emplace_back(current_index);


                CHECK_LT(break_pos, num_breaks);

                do {
                    const auto next_visit_strictly_precedes_break =
                            data.Max(dimension_->CumulVar(next_index)) <= data.StartMin(break_intervals[break_pos]);
                    if (next_visit_strictly_precedes_break) {
                        break;
                    }

                    const auto break_strictly_precedes_next_visit =
                            data.StartMax(break_intervals[break_pos]) <= data.Min(dimension_->CumulVar(next_index));
                    if (!break_strictly_precedes_next_visit) {
                        const auto time_after_break = std::max(current_time, data.StartMin(break_intervals[break_pos]))
                                                      + data.DurationMin(break_intervals[break_pos]);
                        const auto time_after_next_visit = std::max(current_time, data.Min(dimension_->CumulVar(next_index)))
                                                           + records_.at(next_index).duration
                                                           + model_->GetArcCostForVehicle(current_index, next_index, vehicle);
                        const auto break_weakly_precedes_next_visit = (time_after_break <= data.Min(dimension_->CumulVar(next_index))) ||
                                                                      (data.StartMax(break_intervals[break_pos]) <= time_after_next_visit);
                        const auto next_visit_weakly_precedes_break = (time_after_next_visit <= data.StartMin(break_intervals[break_pos])) ||
                                                                      (data.Max(dimension_->CumulVar(next_index)) <=
                                                                       time_after_break); // performing a visit does not affect break or visit cannot be performed after break

                        if (next_visit_weakly_precedes_break) {
                            break;
                        }

                        if (!break_weakly_precedes_next_visit) {
                            // regardless of the waiting time we prefer doing a visit before break if both options are possible
                            const auto break_before_next_visit_waiting_time = std::max(0l, data.StartMin(break_intervals[break_pos]) - current_time)
                                                                              + std::max(0l,
                                                                                         data.Min(dimension_->CumulVar(next_index)) -
                                                                                         time_after_break);
                            const auto next_visit_before_break_waiting_time =
                                    std::max(0l, data.Min(dimension_->CumulVar(next_index)) - current_time) +
                                    std::max(0l, time_after_next_visit - data.StartMin(break_intervals[break_pos]));

//                    // if can do visit without waiting then do the visit
                            if (current_time >= data.Min(dimension_->CumulVar(next_index))) {
                                break;
                            }

                            if (next_visit_before_break_waiting_time <= break_before_next_visit_waiting_time) {
                                break;
                            }
                        }
                    }

                    current_time = std::max(current_time, data.StartMin(break_intervals[break_pos])) + data.DurationMin(break_intervals[break_pos]);
                    path.emplace_back(-break_pos);

                    ++break_pos;
                } while (break_pos < num_breaks);

                current_index = next_index;
            }

            path.emplace_back(current_index);
            for (; break_pos < num_breaks; ++break_pos) {
                path.emplace_back(-break_pos);
            }

            return path;
        }

        struct PartialPath {
            PartialPath(std::size_t size)
                    : current_time{0},
                      travel_time{0},
                      node_next_pos{0},
                      break_next_pos{0},
                      path(size, 0) {}

            int64 current_time;
            int64 travel_time;
            std::size_t node_next_pos;
            std::size_t break_next_pos;
            std::vector<int64> path;
        };

        template<typename DataSource>
        std::vector<int64> BuildPath(int vehicle, const DataSource &data) {
            std::vector<int64> node_path;
            {
                int64 current_node = model_->Start(vehicle);
                while (!model_->IsEnd(current_node)) {
                    node_path.emplace_back(current_node);
                    current_node = data.Value(model_->NextVar(current_node));
                }
                node_path.emplace_back(current_node);
            }

            const auto num_nodes = node_path.size();
            if (num_nodes == 2) {
                return {};
            }

            const auto &break_intervals = dimension_->GetBreakIntervalsOfVehicle(vehicle);
            const auto num_breaks = break_intervals.size();
            CHECK_GE(num_breaks, 2);

            if (num_breaks == 2) {
                node_path.emplace_back(-1);
                return node_path;
            }

            // deliberately skip the first break node since its end time is encoded as the start time of the first regular node
            const auto path_size = num_breaks + num_nodes - 1;

            PartialPath initial_path{path_size};
            initial_path.break_next_pos = 1;
            initial_path.current_time = data.StartMin(break_intervals[0]) + data.DurationMin(break_intervals[0]);

            std::vector<PartialPath> partial_paths{initial_path};
            std::size_t current_path_pos = 0;

            // TODO: refactor common operations on paths
            // TODO: avoid using reference to the path
            while (current_path_pos < partial_paths.size()) {
                for (std::size_t path_pos = partial_paths.at(current_path_pos).break_next_pos + partial_paths.at(current_path_pos).node_next_pos - 1;
                     path_pos < path_size; ++path_pos) {
                    auto &current_path = partial_paths.at(current_path_pos);

                    if (current_path.node_next_pos == num_nodes) {
                        CHECK_LT(current_path.break_next_pos, num_breaks);

                        const auto &interval = break_intervals.at(current_path.break_next_pos);
                        if (current_path.current_time > data.StartMax(interval)) {
                            break;
                        }

                        CHECK_EQ(current_path.travel_time, 0);
                        current_path.path[path_pos] = -current_path.break_next_pos;
                        current_path.current_time = std::max(current_path.current_time, data.StartMin(interval)) + data.DurationMin(interval);
                        ++current_path.break_next_pos;
                        continue;
                    }

                    if (current_path.break_next_pos == num_breaks) {
                        CHECK_LT(current_path.node_next_pos, num_nodes);
                        const std::size_t current_node = node_path[current_path.node_next_pos];

                        if (current_path.current_time + current_path.travel_time > data.Max(dimension_->CumulVar(current_node))) {
                            break;
                        }

                        int64 next_visit_travel_time = 0;
                        if (current_path.node_next_pos + 1 < num_nodes) {
                            const auto next_node = node_path[current_path.node_next_pos + 1];
                            next_visit_travel_time = model_->GetArcCostForVehicle(current_node, next_node, vehicle);
                        }

                        current_path.path[path_pos] = current_node;
                        current_path.current_time =
                                std::max(current_path.current_time + current_path.travel_time, data.Min(dimension_->CumulVar(current_node)))
                                + records_.at(current_node).duration;
                        current_path.travel_time = next_visit_travel_time;
                        ++current_path.node_next_pos;
                        continue;
                    }

                    const auto &interval = break_intervals.at(current_path.break_next_pos);
                    if (current_path.current_time > data.StartMax(interval)) {
                        break;
                    }

                    const std::size_t current_node = node_path[current_path.node_next_pos];
                    if (current_path.current_time > data.Max(dimension_->CumulVar(current_node))) {
                        break;
                    }

                    int64 next_visit_travel_time = 0;
                    if (current_path.node_next_pos + 1 < num_nodes) {
                        int64 next_node = node_path[current_path.node_next_pos + 1];
                        next_visit_travel_time = model_->GetArcCostForVehicle(current_node, next_node, vehicle);
                    }

                    const auto time_after_break = std::max(current_path.current_time, data.StartMin(interval)) + data.DurationMin(interval);
                    const auto time_after_visit =
                            std::max(current_path.current_time + current_path.travel_time,
                                     data.Min(dimension_->CumulVar(current_node))) + records_.at(current_node).duration;
                    const auto waiting_for_break = std::max(0l, data.StartMin(interval) - current_path.current_time);

                    const auto next_visit_preference = data.Max(dimension_->CumulVar(current_node)) < data.StartMin(interval)
                                                       || time_after_visit < data.StartMin(interval)
                                                       || data.Max(dimension_->CumulVar(current_node)) < time_after_break
                                                       || current_path.break_next_pos == num_breaks - 1; // put the last break at the end of the path
                    const auto break_preference = data.StartMax(interval) < data.Min(dimension_->CumulVar(current_node))
                                                  || time_after_break < data.Min(dimension_->CumulVar(current_node))
                                                  || data.StartMax(interval) < time_after_visit;

                    if (next_visit_preference && break_preference) {
                        break;
                    }

                    if (break_preference) {
                        current_path.path[path_pos] = -current_path.break_next_pos;
                        current_path.travel_time = std::max(0l, current_path.travel_time - waiting_for_break);
                        current_path.current_time = time_after_break;
                        ++current_path.break_next_pos;
                    } else if (next_visit_preference) {
                        current_path.path[path_pos] = current_node;
                        current_path.current_time = time_after_visit;
                        current_path.travel_time = next_visit_travel_time;
                        ++current_path.node_next_pos;
                    } else {
                        PartialPath path_copy = current_path;

                        current_path.path[path_pos] = -current_path.break_next_pos;
                        current_path.travel_time = std::max(0l, current_path.travel_time - waiting_for_break);
                        current_path.current_time = time_after_break;
                        ++current_path.break_next_pos;

                        path_copy.path[path_pos] = current_node;
                        path_copy.current_time = time_after_visit;
                        path_copy.travel_time = next_visit_travel_time;
                        ++path_copy.node_next_pos;
                        partial_paths.emplace_back(path_copy);
                    }
                }

                ++current_path_pos;
            }

            const auto num_paths = partial_paths.size();
            std::size_t result_pos = 0;
            for (; result_pos < num_paths && partial_paths[result_pos].path[path_size - 1] == 0; ++result_pos);
            CHECK_LT(result_pos, num_paths);

            for (std::size_t candidate_pos = result_pos + 1; candidate_pos < num_paths; ++candidate_pos) {
                if (partial_paths[candidate_pos].path[path_size - 1] == 0) {
                    continue;
                }

                // TODO USE total waiting time to distinguish between paths
//                CHECK_NE(partial_paths[candidate_pos].current_time, partial_paths[result_pos].current_time);

                if (partial_paths[candidate_pos].current_time < partial_paths[result_pos].current_time) {
                    result_pos = candidate_pos;
                }
            }

            return partial_paths[result_pos].path;
        }

        template<typename DataSource>
        void UpdatePath(int vehicle, const DataSource &data) {
            const auto path = BuildPath<DataSource>(vehicle, data);
            UpdatePathRecords<DataSource>(vehicle, path, data);

            const auto num_samples = duration_sample_.size();
            for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
                PropagateNode(model_->Start(vehicle), scenario);
            }

            ComputePathDelay(vehicle);
        }

        template<typename DataSource>
        void UpdatePathRecords(int vehicle, const std::vector<int64> &path, const DataSource &data) {
            if (path.empty()) {
                return;
            }

            const auto path_size = path.size();
            CHECK_LT(path[path_size - 1], 0);

            const auto &break_intervals = dimension_->GetBreakIntervalsOfVehicle(vehicle);
            int64 current_node = -1;
            int64 last_break_min = 0;
            int64 last_break_duration = 0;
            int64 total_break_duration = 0;

            for (std::size_t path_pos = 0; path_pos < path_size; ++path_pos) {
                const auto element = path[path_pos];

                if (element >= 0) {
                    if (current_node == -1) {
                        current_node = element;
                        continue;
                    }
                    const auto next_node = element;

                    auto &record = records_.at(current_node);
                    record.next = next_node;
                    record.travel_time = model_->GetArcCostForVehicle(current_node, next_node, vehicle);
                    record.break_min = last_break_min + last_break_duration - total_break_duration;
                    record.break_duration = total_break_duration;

                    std::fill(std::begin(start_.at(current_node)), std::end(start_.at(current_node)), duration_sample_.start_min(current_node));
                    std::fill(std::begin(delay_.at(current_node)), std::end(delay_.at(current_node)), 0);

                    current_node = next_node;
                    last_break_duration = 0;
                    last_break_min = 0;
                    total_break_duration = 0;
                } else {
                    const auto interval = break_intervals.at(-element);
                    last_break_min = data.StartMin(interval);
                    last_break_duration = data.DurationMin(interval);
                    total_break_duration += data.DurationMin(interval);
                }
            }
            CHECK_EQ(records_.at(current_node).break_min, 0);
            CHECK_EQ(records_.at(current_node).travel_time, 0);
            CHECK_EQ(records_.at(current_node).break_duration, 0);
            CHECK_EQ(records_.at(current_node).next, 0);
            std::fill(std::begin(start_.at(current_node)), std::end(start_.at(current_node)), duration_sample_.start_min(current_node));
            std::fill(std::begin(delay_.at(current_node)), std::end(delay_.at(current_node)), 0);
        }

        void ComputePathDelay(int vehicle);

        void ComputeAllPathsDelay();

        void PropagateNode(int64 index, std::size_t scenario);

        void PropagateNodeWithSiblings(int64 index, std::size_t scenario, std::unordered_set<int64> &siblings_updated);

        int64 GetArrivalTime(const TrackRecord &record, std::size_t scenario) const;

        const operations_research::RoutingDimension *dimension_;
        const operations_research::RoutingModel *model_;
        DurationSample duration_sample_;

        std::vector<TrackRecord> records_;
        std::vector<std::vector<int64>> start_;
        std::vector<std::vector<int64>> delay_;
    };
}


#endif //ROWS_DELAY_TRACKER_H
