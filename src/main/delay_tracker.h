#ifndef ROWS_DELAY_TRACKER_H
#define ROWS_DELAY_TRACKER_H

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/adjacency_list.hpp>

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
        void PrintPathNoDetails(int vehicle, const DataSource &data) {
            std::stringstream msg;
            msg << "Vehicle " << vehicle << ": [ ";
            int64 current_index = model_->Start(vehicle);
            msg << current_index;

            current_index = data.Value(model_->NextVar(current_index));
            while (!model_->IsEnd(current_index)) {
                msg << ", " << current_index;

                int64 sibling_index = duration_sample_.sibling(current_index);
                if (sibling_index != -1) {
                    msg << " (" << sibling_index << ")";
                }

                current_index = data.Value(model_->NextVar(current_index));
            }
            msg << "]";

            LOG(INFO) << msg.str();
        }

        template<typename DataSource>
        void UpdateAllPathsFromSource(const DataSource &data) {
            for (std::size_t index = 0; index < records_.size(); ++index) {
                std::fill(std::begin(start_[index]), std::end(start_[index]), duration_sample_.start_min(index));
                std::fill(std::begin(delay_[index]), std::end(delay_[index]), 0);
                records_[index].next = -1;
            }

            for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
                const auto path = BuildPathFromSource<decltype(data)>(vehicle, data);

//                if(!path.empty()) {
//                    std::stringstream msg;
//                    msg << "Vehicle " << vehicle << ": [ ";
//                    msg << path[0];
//                    for (std::size_t pos = 1; pos < path.size(); ++pos) {
//                        msg << ", " << path[pos];
//                    }
//                    msg << "]";
//                    LOG(INFO) << msg.str();
//                }

                UpdatePathRecords<decltype(data)>(vehicle, path, data);
            }

            ComputeAllPathsDelay(data);
        }

        template<typename DataSource>
        void ComputeAllPathsDelay(DataSource &data_source) {
            using Graph = boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS>;
            using Vertex = int64;
            using Edge = std::pair<int64, int64>;

            std::vector<Edge> edges;
            for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
                int64 current_node = model_->Start(vehicle);
                while (!model_->IsEnd(current_node)) {
                    const auto &current_record = records_[current_node];
                    CHECK_EQ(current_node, current_record.index);

                    const auto next_node = current_record.next;
                    if (next_node == -1) { break; }

                    edges.emplace_back(current_node, next_node);

                    int64 sibling_node = duration_sample_.sibling(current_node);
                    if (sibling_node != -1) {
                        if (current_node < sibling_node) {
                            edges.emplace_back(current_node, sibling_node);
                            edges.emplace_back(sibling_node, next_node);
                        }
                    }

                    current_node = next_node;
                }
            }

            const auto num_vertices = static_cast<Graph::vertices_size_type>(solver_.index_manager().num_indices());
            std::vector<bool> visited_nodes;
            visited_nodes.resize(num_vertices);
            Graph dag{std::cbegin(edges), std::cend(edges), num_vertices};

            for (Vertex vertex = 0; vertex < num_vertices; ++vertex) {
                std::vector<Vertex> outgoing_vertices;
                boost::graph_traits<Graph>::out_edge_iterator out_ei, out_ei_end;
                for (std::tie(out_ei, out_ei_end) = boost::out_edges(vertex, dag); out_ei != out_ei_end; ++out_ei) {
                    const auto target = boost::target(*out_ei, dag);
                    visited_nodes[target] = true;
                    outgoing_vertices.emplace_back(target);
                }

                const auto degree = outgoing_vertices.size();
                if (degree == 0) {
                    continue;
                } else if (degree == 1) {
                    const auto outgoing_node = outgoing_vertices.front();
                    CHECK(model_->IsEnd(vertex) || (!duration_sample_.has_sibling(vertex) && records_[vertex].next == outgoing_node));
                } else {
                    CHECK_EQ(degree, 2);
                    CHECK(duration_sample_.is_visit(vertex));
                    CHECK(duration_sample_.has_sibling(vertex));

                    int64 other_sibling = duration_sample_.sibling(vertex);
                    CHECK(std::find(std::cbegin(outgoing_vertices), std::cend(outgoing_vertices), records_[vertex].next) !=
                          std::cend(outgoing_vertices));

                    if (vertex < other_sibling) {
                        CHECK(std::find(std::cbegin(outgoing_vertices), std::cend(outgoing_vertices), other_sibling) !=
                              std::cend(outgoing_vertices));
                    } else {
                        CHECK(std::find(std::cbegin(outgoing_vertices), std::cend(outgoing_vertices), records_[other_sibling].next) !=
                              std::cend(outgoing_vertices));
                    }
                }
            }

            for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
                visited_nodes[model_->Start(vehicle)] = true;
            }

            std::vector<int64> reverse_sorted_vertices;
            boost::topological_sort(dag, std::back_inserter(reverse_sorted_vertices));

            const auto num_samples = duration_sample_.size();
            for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
                for (auto it = std::crbegin(reverse_sorted_vertices); it != std::crend(reverse_sorted_vertices); ++it) {
                    const auto index = *it;
                    if (model_->IsEnd(index)) { continue; }
                    if (!visited_nodes[index]) { continue; }

                    const auto sibling_index = duration_sample_.sibling(index);
                    if (sibling_index >= 0) {
                        const auto max_start_time = std::max(start_[index][scenario], start_[sibling_index][scenario]);
                        start_[index][scenario] = max_start_time;
                        start_[sibling_index][scenario] = max_start_time;
                    }

                    const auto &record = records_[index];
                    auto arrival_time = start_[index][scenario] + duration_sample_.duration(index, scenario) + record.travel_time;
                    if (arrival_time > record.break_min) {
                        arrival_time += record.break_duration;
                    } else {
                        arrival_time = record.break_min + record.break_duration;
                    }

                    if (arrival_time > start_[record.next][scenario]) {
                        start_[record.next][scenario] = arrival_time;
                    }
                }

                for (int64 index = 0; index < solver_.index_manager().num_indices(); ++index) {
                    int64 sibling = duration_sample_.sibling(index);
                    if (sibling != -1) {
                        CHECK_EQ(start_[index][scenario], start_[sibling][scenario]);
                    }
                }
            }

//            for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
//                for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
//                    PropagateNodeWithBreaks(model_->Start(vehicle), scenario, data_source);
//                }
//
//                std::queue<int64> processing_queue;
//                for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
//                    int64 current_node = model_->Start(vehicle);
//                    while (!model_->IsEnd(current_node)) {
//                        int64 sibling_node = duration_sample_.sibling(current_node);
//                        if (sibling_node != -1) {
//                            if (start_[current_node][scenario] != start_[sibling_node][scenario]) {
//                                int64 new_start = std::max(start_[current_node][scenario], start_[sibling_node][scenario]);
//                                if (start_[current_node][scenario] < new_start) {
//                                    start_[current_node][scenario] = new_start;
//                                    processing_queue.emplace(current_node);
//                                } else if (start_[sibling_node][scenario] < new_start) {
//                                    start_[sibling_node][scenario] = new_start;
//                                    processing_queue.emplace(sibling_node);
//                                }
//                            }
//                        }
//                        current_node = records_[current_node].next;
//                    }
//                }
//
//                while (!processing_queue.empty()) {
//                    const auto current_node = processing_queue.front();
//                    processing_queue.pop();
//
//                    PropagateNodeWithSiblingsNoBreaks(current_node, scenario, processing_queue, data_source);
//                }
//            }

            for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
                ComputePathDelay(vehicle);
            }
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

            int64 TotalNormalizedSlack() const {
                int64 total_normalized_slack = 0;
                const std::size_t num_slacks = slack.size();
                for (std::size_t pos = 2; pos < num_slacks; ++pos) {
                    total_normalized_slack += pos * slack[pos];
                }
                return total_normalized_slack;
            }

            int64 current_time;
            int64 travel_time;
            std::size_t node_next_pos;
            std::size_t break_next_pos;
            std::vector<int64> path;
            std::vector<int64> slack;
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
                    visit_slack_time = data_.Max(dimension_->CumulVar(current_node)) - path.current_time - path.travel_time;
                } else {
                    time_after_visit = kint64max;
                    next_travel_time = 0;
                    visit_slack_time = 0;
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
                path.slack.emplace_back(visit_slack_time);
            }

            int64 next_travel_time{};
            int64 time_after_visit{};
            int64 visit_slack_time{};
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

        static const int64 MAX_START_TIME = 48 * 60 * 60;

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

            int64 result_slack = partial_paths[result_pos].TotalNormalizedSlack();
            for (std::size_t candidate_pos = result_pos + 1; candidate_pos < num_paths; ++candidate_pos) {
                if (!partial_paths[candidate_pos].IsComplete()) {
                    continue;
                }

                int64 candidate_slack = partial_paths[candidate_pos].TotalNormalizedSlack();
                if (result_slack < candidate_slack) {
                    result_pos = candidate_pos;
                    result_slack = candidate_slack;
                }
            }

            return partial_paths[result_pos].path;
        }

        template<typename DataSource>
        void UpdatePath(int vehicle, const DataSource &data) {
            const auto path = BuildPathFromSource<DataSource>(vehicle, data);
            UpdatePathRecords<DataSource>(vehicle, path, data);

            const auto num_samples = duration_sample_.size();
            for (std::size_t scenario = 0; scenario < num_samples; ++scenario) {
                PropagateNodeWithBreaks(model_->Start(vehicle), scenario, data);
            }

            ComputePathDelay(vehicle);
        }

        template<typename DataSource>
        void UpdatePathRecords(int vehicle, const std::vector<int64> &path, const DataSource &data) {
            if (path.empty()) {
                const auto start_node = model_->Start(vehicle);
                const auto end_node = model_->End(vehicle);

                records_[start_node].break_min = 0;
                records_[start_node].travel_time = 0;
                records_[start_node].break_duration = 0;
                records_[start_node].next = end_node;

                CHECK_EQ(records_[end_node].break_min, 0);
                CHECK_EQ(records_[end_node].travel_time, 0);
                CHECK_EQ(records_[end_node].break_duration, 0);
                CHECK_EQ(records_[end_node].next, -1);
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
        }

        void ComputePathDelay(int vehicle);

        template<typename DataSource>
        void PropagateNodeWithBreaks(int64 index, std::size_t scenario, const DataSource &data_source) {
            auto current_index = index;
            while (!model_->IsEnd(current_index)) {
                const auto &current_record = records_[current_index];
                const auto arrival_time = GetArrivalTimeWithBreak(current_record, scenario);
                CHECK_LT(arrival_time, MAX_START_TIME);
                CHECK_LE(start_[current_record.next][scenario], arrival_time);
                CHECK_NE(current_record.next, -1);

                start_[current_record.next][scenario] = arrival_time;
                current_index = current_record.next;
            }
        }

        template<typename DataSource>
        void PropagateNodeWithSiblingsNoBreaks(int64 index,
                                               std::size_t scenario,
                                               std::queue<int64> &siblings_updated,
                                               const DataSource &data_source) {
            auto current_index = index;
            while (!model_->IsEnd(current_index)) {
                const auto &current_record = records_[current_index];
                const auto arrival_time = GetArrivalTimeNoBreak(current_record, scenario);
                if (arrival_time >= MAX_START_TIME) {
                    for (int vehicle = 0; vehicle < model_->vehicles(); ++vehicle) {
                        PrintPathNoDetails<DataSource>(vehicle, data_source);
                    }
                    LOG(FATAL) << "Assertion Failed";
                }

                CHECK_NE(current_record.next, -1);

                if (start_[current_record.next][scenario] < arrival_time) {
                    start_[current_record.next][scenario] = arrival_time;
                    if (duration_sample_.has_sibling(current_record.next)) {
                        const auto sibling = duration_sample_.sibling(current_record.next);
                        if (start_[sibling][scenario] < arrival_time) {
                            start_[sibling][scenario] = arrival_time;
                            siblings_updated.emplace(sibling);
                        }
                    }
                } else { break; }

                current_index = current_record.next;
            }
        }


        int64 GetArrivalTimeWithBreak(const TrackRecord &record, std::size_t scenario) const;

        int64 GetArrivalTimeNoBreak(const TrackRecord &record, std::size_t scenario) const;

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
