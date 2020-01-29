#ifndef ROWS_DELAY_TRACKER_H
#define ROWS_DELAY_TRACKER_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "duration_sample.h"

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

        inline TrackRecord &Record(int64 node) { return records_[node]; }

        inline const std::vector<int64> &Delay(int64 node) const { return delay_[node]; }

        int64 GetMeanDelay(int64 node) const;

        int64 GetDelayProbability(int64 node) const;

        inline const operations_research::RoutingModel *model() const { return model_; }

        void UpdateAllPaths();

        void UpdateAllPaths(operations_research::Assignment const *assignment);

        void UpdatePath(int vehicle);

        void UpdatePath(int vehicle, operations_research::Assignment const *assignment);

        std::vector<int64> BuildPath(int vehicle, operations_research::Assignment const *assignment);

    private:

        template<typename DataSource>
        void PrintPathDetails(int vehicle, const DataSource &data) {
            std::stringstream msg;
            msg << "Vehicle: " << vehicle << std::endl;

            msg << "Breaks: " << std::endl;
            for (const auto &interval : dimension_->GetBreakIntervalsOfVehicle(vehicle)) {
                msg << "[" << data.StartMin(interval) << ", " << data.StartMax(interval) << "] " << data.DurationMin(interval) << std::endl;
            }

            msg << "Visits: " << std::endl;
            int64 current_index = model_->Start(vehicle);
            while (!model_->IsEnd(current_index)) {
                int64 next_index = data.Value(model_->NextVar(current_index));
                msg << "[" << data.Min(dimension_->CumulVar(current_index))
                    << ", " << data.Max(dimension_->CumulVar(current_index)) << "] " << records_.at(current_index).duration
                    << ", " << model_->GetArcCostForVehicle(current_index, next_index, vehicle) << std::endl;

                current_index = next_index;
            }

            LOG(INFO) << msg.str();
        }

        template<typename DataSource>
        void UpdateAllPathsFromSource(const DataSource &data) {
            std::unordered_set<int64> visited_indices;
            for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
                const auto path = BuildPathFromSource<decltype(data)>(vehicle, data);

                for (const auto element : path) {
                    if (element >= 0) {
                        visited_indices.emplace(element);
                    }
                }

                UpdatePathRecords<decltype(data)>(vehicle, path, data);
            }

            // TODO: loop breaker
            for (int64 index = 0; index < solver_.index_manager().num_indices(); ++index) {
                if (visited_indices.find(index) == std::cend(visited_indices)) {
                    records_.at(index).next = -1;
                    continue;
                }

                if (model_->IsEnd(index)) {
                    CHECK_EQ(records_.at(index).next, -1);
                    continue;
                }

                CHECK((records_.at(index).next >= 0 && !model_->IsStart(records_.at(index).next)) || records_.at(index).next == -1);
                CHECK_NE(records_.at(index).next, records_.at(index).index);
            }

            ComputeAllPathsDelay();
        }

        struct PartialPath {
            explicit PartialPath(std::size_t size)
                    : current_time{0},
                      travel_time{0},
                      node_next_pos{0},
                      break_next_pos{0},
                      path(size, 0) {}

            bool IsComplete() const { return path[path.size() - 1] < 0; }

            inline std::size_t CurrentPosition() const { return break_next_pos + node_next_pos - 1; }

            int64 current_time;
            int64 travel_time;
            std::size_t node_next_pos;
            std::size_t break_next_pos;
            std::vector<int64> path;
        };

        template<typename DataSource>
        static std::vector<int64> GetNodePath(int vehicle, const operations_research::RoutingModel *model, const DataSource &data) {
            std::vector<int64> path;

            int64 current_node = model->Start(vehicle);
            while (!model->IsEnd(current_node)) {
                path.emplace_back(current_node);
                current_node = data.Value(model->NextVar(current_node));
            }
            path.emplace_back(current_node);

            return path;
        }

        template<typename DataSource>
        struct PathBuilder {
            PathBuilder(const DataSource &data,
                        const std::vector<TrackRecord> &records,
                        const operations_research::RoutingDimension *dimension,
                        int vehicle)
                    : data_{data},
                      records_{records},
                      dimension_{dimension},
                      vehicle_{vehicle},
                      breaks{dimension_->GetBreakIntervalsOfVehicle(vehicle_)},
                      nodes{GetNodePath<DataSource>(vehicle_, dimension_->model(), data_)} {}

            void Process(const PartialPath &path) {
                const auto has_visit = path.node_next_pos < nodes.size();
                const auto has_break = path.break_next_pos < breaks.size();
                completed = has_visit || has_break;

                if (has_visit) {
                    const auto current_node = nodes[path.node_next_pos];

                    if (path.current_time + path.travel_time > data_.Max(dimension_->CumulVar(current_node))) {
                        Fail();
                        return;
                    }

                    if (path.node_next_pos + 1 < nodes.size()) {
                        const auto next_node = nodes[path.node_next_pos + 1];
                        next_travel_time = dimension_->model()->GetArcCostForVehicle(current_node, next_node, vehicle_);
                    } else {
                        next_travel_time = 0;
                    }

                    time_after_visit = std::max(path.current_time + path.travel_time, data_.Min(dimension_->CumulVar(current_node)))
                                       + records_[current_node].duration;
                } else {
                    time_after_visit = kint64max;
                    next_travel_time = 0;
                }

                if (has_break) {
                    const auto &interval = breaks[path.break_next_pos];

                    if (path.current_time > data_.StartMax(interval)) {
                        Fail();
                        return;
                    }

                    time_after_break = std::max(path.current_time, data_.StartMin(interval)) + data_.DurationMin(interval);
                    waiting_time_for_break = std::max(static_cast<int64>(0l), data_.StartMin(interval) - path.current_time);
                } else {
                    time_after_break = kint64max;
                    waiting_time_for_break = 0;
                }

                if (has_visit && has_break) {
                    const auto &interval = breaks[path.break_next_pos];
                    const auto current_node = nodes[path.node_next_pos];

                    visit_preferred = data_.Max(dimension_->CumulVar(current_node)) < data_.StartMin(interval)
                                      || time_after_visit < data_.StartMin(interval)
                                      || data_.Max(dimension_->CumulVar(current_node)) < time_after_break
                                      || path.break_next_pos == breaks.size() - 1; // put the last break at the end of the path

                    break_preferred = data_.StartMax(interval) < data_.Min(dimension_->CumulVar(current_node))
                                      || time_after_break < data_.Min(dimension_->CumulVar(current_node))
                                      || data_.StartMax(interval) < time_after_visit;

                    if (vehicle_ == 46) {
                        LOG(INFO) << "Visit [" << data_.Min(dimension_->CumulVar(current_node)) << ", "
                                  << data_.Max(dimension_->CumulVar(current_node)) << "] vs Break [" << data_.StartMin(interval) << ", "
                                  << data_.StartMax(interval) << "]";
                        LOG(INFO) << data_.Max(dimension_->CumulVar(current_node)) << " <  " << data_.StartMin(interval);
                        LOG(INFO) << time_after_visit << " < " << data_.StartMin(interval);
                        LOG(INFO) << data_.Max(dimension_->CumulVar(current_node)) << " < " << time_after_break;
                        LOG(INFO) << "VisitPreferred: " << visit_preferred;

                        LOG(INFO) << data_.StartMax(interval) << " < " << data_.Min(dimension_->CumulVar(current_node));
                        LOG(INFO) << time_after_break << " < " << data_.Min(dimension_->CumulVar(current_node));
                        LOG(INFO) << data_.StartMax(interval) << " < " << time_after_visit;
                        LOG(INFO) << "BreakPreferred: " << break_preferred;
                    }

                    if (visit_preferred && break_preferred) {
                        Fail();
                        return;
                    }

                    failed = false;
                } else if (has_break) {
                    break_preferred = true;
                    visit_preferred = false;
                    failed = false;
                } else if (has_visit) {
                    break_preferred = false;
                    visit_preferred = true;
                    failed = false;
                } else {
                    Fail();
                }
            }

            void PerformBreak(PartialPath &path) {
                std::size_t path_pos = path.break_next_pos + path.node_next_pos - 1;

                path.path[path_pos] = -path.break_next_pos;
                path.travel_time = std::max(0l, path.travel_time - waiting_time_for_break);
                path.current_time = time_after_break;
                ++path.break_next_pos;
            }

            void PerformVisit(PartialPath &path) {
                std::size_t path_pos = path.break_next_pos + path.node_next_pos - 1;

                path.path[path_pos] = nodes[path.node_next_pos];
                path.current_time = time_after_visit;
                path.travel_time = next_travel_time;
                ++path.node_next_pos;
            }

            int64 next_travel_time{};
            int64 time_after_visit{};
            int64 time_after_break{};
            int64 waiting_time_for_break{};
            bool visit_preferred{};
            bool break_preferred{};
            bool failed{};
            bool completed{};

        private:
            void Fail() {
                next_travel_time = 0;
                time_after_visit = kint64max;
                time_after_break = kint64max;
                waiting_time_for_break = 0;
                break_preferred = false;
                visit_preferred = false;
                failed = true;
            }

            const DataSource &data_;
            const std::vector<TrackRecord> &records_;
            const operations_research::RoutingDimension *dimension_;
            const int vehicle_;

        public:
            const std::vector<operations_research::IntervalVar *> &breaks;
            const std::vector<int64> nodes;
        };

        template<typename DataSource>
        std::vector<int64> BuildPathFromSource(int vehicle, const DataSource &data) {
            PathBuilder<DataSource> builder{data, records_, dimension_, vehicle};

            const auto num_breaks = builder.breaks.size();
            const auto num_nodes = builder.nodes.size();
            CHECK_GE(num_breaks, 2);

            if (num_nodes == 2) {
                return {};
            }

            if (num_breaks == 2) {
                auto node_path = builder.nodes;
                node_path.emplace_back(-1);
                return node_path;
            }

            // deliberately skip the first break node since its end time is encoded as the start time of the first regular node
            const auto path_size = num_breaks + num_nodes - 1;

            PartialPath initial_path{path_size};
            initial_path.break_next_pos = 1;
            initial_path.current_time = data.StartMin(builder.breaks[0]) + data.DurationMin(builder.breaks[0]);

            std::vector<PartialPath> partial_paths{initial_path};
            std::size_t current_path_pos = 0;
            while (current_path_pos < partial_paths.size()) {
                for (std::size_t pos = partial_paths[current_path_pos].CurrentPosition(); pos < path_size; ++pos) {
                    builder.Process(partial_paths[current_path_pos]);

                    if (builder.failed) {
                        break;
                    }

                    if (builder.break_preferred) {

                        if (partial_paths[current_path_pos].node_next_pos == builder.nodes.size()) {
                            CHECK_EQ(partial_paths[current_path_pos].travel_time, 0);
                        }

                        builder.PerformBreak(partial_paths[current_path_pos]);
                        continue;
                    } else if (builder.visit_preferred) {
                        builder.PerformVisit(partial_paths[current_path_pos]);
                        continue;
                    } else {
                        PartialPath path_copy = partial_paths[current_path_pos];

                        builder.PerformVisit(partial_paths[current_path_pos]);
                        builder.PerformBreak(path_copy);

                        partial_paths.emplace_back(std::move(path_copy));
                    }
                }

                ++current_path_pos;
            }

            const auto num_paths = partial_paths.size();
            std::size_t result_pos = 0;
            for (; result_pos < num_paths && !partial_paths[result_pos].IsComplete(); ++result_pos);

            if (result_pos == num_paths) {
                if (model_->solver()->CurrentlyInSolve()) {
                    model_->solver()->Fail();
                } else {
                    std::stringstream msg{"Path does not have a valid sequence of visits and breaks"};
                    msg << std::endl;
                    msg << "Vehicle: " << vehicle << std::endl;
                    msg << "Nodes: [" << builder.nodes[0];
                    for (std::size_t node_pos = 1; node_pos < builder.nodes.size(); ++node_pos) {
                        msg << ", " << builder.nodes[node_pos];
                    }
                    msg << "]" << std::endl;
                    LOG(FATAL) << msg.str();
                }
            }

            for (std::size_t candidate_pos = result_pos + 1; candidate_pos < num_paths; ++candidate_pos) {
                if (!partial_paths[candidate_pos].IsComplete()) {
                    continue;
                }

                // TODO: use total waiting time to distinguish between paths
                // CHECK_NE(partial_paths[candidate_pos].current_time, partial_paths[result_pos].current_time);

                if (partial_paths[candidate_pos].current_time < partial_paths[result_pos].current_time) {
                    result_pos = candidate_pos;
                }
            }

            if (vehicle == 46) {
                PrintPathDetails<decltype(data)>(vehicle, data);
                std::stringstream msg;

                msg << partial_paths[result_pos].path[0];
                for (std::size_t pos = 1; pos < partial_paths[result_pos].path.size(); ++pos) {
                    msg << ", " << partial_paths[result_pos].path[pos];
                }
                LOG(INFO) << msg.str();
            }

            return partial_paths[result_pos].path;
        }

        template<typename DataSource>
        void UpdatePath(int vehicle, const DataSource &data) {
            const auto path = BuildPathFromSource<DataSource>(vehicle, data);
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

                    auto &record = records_[current_node];
                    record.next = next_node;
                    record.travel_time = model_->GetArcCostForVehicle(current_node, next_node, vehicle);
                    record.break_min = last_break_min + last_break_duration - total_break_duration;
                    record.break_duration = total_break_duration;

                    std::fill(std::begin(start_[current_node]), std::end(start_[current_node]), duration_sample_.start_min(current_node));
                    std::fill(std::begin(delay_[current_node]), std::end(delay_[current_node]), 0);

                    current_node = next_node;
                    last_break_duration = 0;
                    last_break_min = 0;
                    total_break_duration = 0;
                } else {
                    const auto interval = break_intervals[-element];
                    last_break_min = data.StartMin(interval);
                    last_break_duration = data.DurationMin(interval);
                    total_break_duration += data.DurationMin(interval);
                }
            }
            CHECK_EQ(records_[current_node].break_min, 0);
            CHECK_EQ(records_[current_node].travel_time, 0);
            CHECK_EQ(records_[current_node].break_duration, 0);
            CHECK_EQ(records_[current_node].next, -1);
            std::fill(std::begin(start_[current_node]), std::end(start_[current_node]), duration_sample_.start_min(current_node));
            std::fill(std::begin(delay_[current_node]), std::end(delay_[current_node]), 0);
        }

        void ComputePathDelay(int vehicle);

        void ComputeAllPathsDelay();

        void PropagateNode(int64 index, std::size_t scenario);

        void PropagateNodeWithSiblings(int64 index, std::size_t scenario, std::unordered_set<int64> &siblings_updated);

        int64 GetArrivalTime(const TrackRecord &record, std::size_t scenario) const;

        const SolverWrapper &solver_;
        const operations_research::RoutingDimension *dimension_;
        const operations_research::RoutingModel *model_;
        DurationSample duration_sample_;

        std::vector<TrackRecord> records_;
        std::vector<std::vector<int64>> start_;
        std::vector<std::vector<int64>> delay_;
    };
}


#endif //ROWS_DELAY_TRACKER_H
