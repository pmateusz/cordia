#include <sstream>

#include "gurobi_c++.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <boost/algorithm/string/join.hpp>

#include "util/validation.h"
#include "util/logging.h"
#include "util/input.h"

#include "location_container.h"
#include "solver_wrapper.h"

// TODO: solve in an aggressive mode
// TODO: create a solution and save it to a file
// TODO: load previous solution and use it as a starting point

DEFINE_string(problem, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists);

DEFINE_string(solution, "", "a file path to the solution file for warm start");
DEFINE_validator(solution, &util::file::IsNullOrExists);

DEFINE_string(maps, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(maps, &util::file::Exists);

void ParseArgs(int argc, char *argv[]) {
    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling\n"
                            "Example: rows-mip"
                            " --problem=problem.json"
                            " --maps=./data/scotland-latest.osrm");

    static const auto REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\n"
                             "problem: %1%\n"
                             "maps: %2%\n") % FLAGS_problem % FLAGS_maps;
}

std::string GetStatus(int gurobi_status) {
    switch (gurobi_status) {
        case GRB_INF_OR_UNBD:
            return "INFINITE_OR_UNBOUNDED";
        case GRB_INFEASIBLE:
            return "INFEASIBLE";
        case GRB_UNBOUNDED:
            return "UNBOUNDED";
        case GRB_OPTIMAL:
            return "OPTIMAL";
    }
}

class SubTourElimination : public GRBCallback {
public:
    SubTourElimination(std::vector<std::vector<std::vector<GRBVar> > > carer_edges);

protected:
    void callback() {

    }

private:
    std::vector<std::vector<std::vector<GRBVar> > > carer_edges_;
};

std::vector<rows::Location> getLocations(const rows::Problem &problem) {
    std::vector<rows::Location> locations;
    for (const auto &visit : problem.visits()) {
        locations.push_back(visit.location().get());
    }
    return locations;
}

class Model {
public:
    static Model Create(rows::Problem problem, osrm::EngineConfig engine_config) {

        std::vector<rows::Location> locations;
        for (const auto &visit : problem.visits()) {
            locations.push_back(visit.location().get());
        }

        rows::CachedLocationContainer location_container(std::cbegin(locations), std::cend(locations), engine_config);
        location_container.ComputeDistances();

        return Model{std::move(problem), std::move(location_container)};
    }

    Model(const rows::Problem &problem, rows::CachedLocationContainer location_container)
            : visits_(problem.visits()),
              carer_diaries_(problem.carers()),
              location_container_(std::move(location_container)),
              node_visits_{},
              multiple_carer_visit_nodes_{},
              begin_depot_node_{0},
              first_visit_node_{1} {
        num_carers_ = carer_diaries_.size();
        carer_breaks_.resize(num_carers_);
        carer_break_nodes_.resize(num_carers_);
        carer_break_start_times_.resize(num_carers_);
        carer_edges_.resize(num_carers_);

        auto current_node = 0;
        for (const auto &visit :visits_) {
            node_visits_[++current_node] = visit;
            if (visit.carer_count() == 2) {
                node_visits_[++current_node] = visit;
                multiple_carer_visit_nodes_.emplace_back(std::make_pair(current_node - 2, current_node - 1));
            }
        }

        last_visit_node_ = current_node;
        end_depot_node_ = last_visit_node_ + 1;

        boost::posix_time::ptime min_visit_start = boost::posix_time::max_date_time;
        for (const auto &visit : visits_) {
            min_visit_start = std::min(min_visit_start, visit.datetime());
        }

        horizon_start_ = boost::posix_time::ptime(min_visit_start.date(), boost::posix_time::time_duration());
        horizon_duration_ = boost::posix_time::seconds(rows::SolverWrapper::SECONDS_IN_DIMENSION);
        boost::posix_time::time_period time_horizon(horizon_start_, horizon_duration_);
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto &carer_pair = carer_diaries_[carer_index];
            carer_breaks_[carer_index] = carer_pair.second[0].Breaks(time_horizon);

            current_node = end_depot_node_;
            // first and last break of a carer denote out of office hours and those are not represented as nodes
            const auto FIRST_INNER_BREAK = 1;
            const auto LAST_INNER_BREAK = carer_breaks_[carer_index].size() - 2;
            for (auto break_index = FIRST_INNER_BREAK; break_index <= LAST_INNER_BREAK; ++break_index) {
                carer_break_nodes_[carer_index].push_back(++current_node);
            }
        }
    }

    void Solve() {
        GRBEnv env = GRBEnv();
        GRBModel model = GRBModel(env);

        Build(model);

        model.optimize();

        const auto solver_status = model.get(GRB_IntAttr_Status);
        LOG(INFO) << "Status " << GetStatus(solver_status);

        if (solver_status == GRB_OPTIMAL) {
            std::vector<std::vector<decltype(carer_edges_)::size_type> > carer_paths;

            static const auto BLANK_NODE = -1;

            for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
                const auto carer_num_nodes = carer_edges_[carer_index].size();
                std::vector<int> next_visit_nodes(carer_num_nodes, BLANK_NODE);
                std::vector<std::vector<int> > next_break_nodes(carer_num_nodes);

                for (auto from_node = begin_depot_node_; from_node <= end_depot_node_; ++from_node) {
                    for (auto to_node = begin_depot_node_; to_node < carer_num_nodes; ++to_node) {
                        const auto value = carer_edges_[carer_index][from_node][to_node].get(GRB_DoubleAttr_X);
                        CHECK(value == 0.0 || value == 1.0);
                        if (value == 1.0) {
                            if (to_node <= end_depot_node_) { // handle visit
                                if (from_node <= end_depot_node_) { // coming back from prior breaks is ignored
                                    CHECK_EQ(next_visit_nodes.at(from_node), BLANK_NODE) << " at " << to_node;
//                            if (next_visit_nodes.at(from_node) != BLANK_NODE) {
//
//                                for (auto local_to_node = BEGIN_DEPOT; local_to_node < carer_num_nodes; ++local_to_node) {
//                                    LOG(INFO) << from_node << " : " << local_to_node << " : " << carer_edges[carer_index][from_node][local_to_node].get(GRB_DoubleAttr_X);
//                                }
//
//                                CHECK(false);
//                            }
                                    next_visit_nodes.at(from_node) = to_node;
                                }
                            } else { // handle break
                                next_break_nodes.at(from_node).push_back(to_node);
                            }
                        }
                    }
                }

                std::vector<decltype(carer_edges_)::size_type> carer_path;
                auto current_visit_node = begin_depot_node_;
                while (current_visit_node != BLANK_NODE) {
                    carer_path.push_back(current_visit_node);

                    for (auto break_index : next_break_nodes[current_visit_node]) {
                        carer_path.push_back(break_index);
                    }

                    const auto next_visit_node = next_visit_nodes[current_visit_node];

                    CHECK_NE(current_visit_node, next_visit_node) << "Current node (" << current_visit_node
                                                                  << ") must not be equal to the next node ("
                                                                  << next_visit_node << ")";
                    current_visit_node = next_visit_node;
                }

                carer_paths.emplace_back(std::move(carer_path));
            }

            std::stringstream output_msg;
            for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
                output_msg << "Carer " << carer_index << ": ";

                auto &carer_path = carer_paths[carer_index];

                CHECK(!carer_path.empty());
                output_msg << carer_path[0];
                const auto carer_path_size = carer_path.size();
                for (auto node_index = 1; node_index < carer_path_size; ++node_index) {
                    output_msg << " -> " << carer_path[node_index];
                }

                output_msg << std::endl;
            }

            LOG(INFO) << output_msg.str();
        }
    }

private:

    void Build(GRBModel &model) {
        // define edges
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            // 2 depots plus all visit nodes (multiple carer visits are counted twice) plus breaks
            // the number of breaks depends on the carer
            const auto carer_num_nodes = 2 + node_visits_.size() + carer_break_nodes_[carer_index].size();

            std::vector<std::vector<GRBVar> > edges(carer_num_nodes, std::vector<GRBVar>());
            for (decltype(edges)::size_type in_index = 0; in_index < carer_num_nodes; ++in_index) {
                for (decltype(edges)::size_type out_index = 0; out_index < carer_num_nodes; ++out_index) {
                    std::string label = "k_" + std::to_string(carer_index) + "_";
                    if (in_index == begin_depot_node_) {
                        label += "b";
                    } else if (in_index == end_depot_node_) {
                        label += "e";
                    } else if (in_index > end_depot_node_) {
                        label += "b" + std::to_string(in_index);
                    } else {
                        label += "v" + std::to_string(in_index);
                    }

                    if (out_index == begin_depot_node_) {
                        label += "b";
                    } else if (out_index == end_depot_node_) {
                        label += "e";
                    } else if (out_index > end_depot_node_) {
                        label += "b" + std::to_string(out_index);
                    } else {
                        label += "v" + std::to_string(out_index);
                    }

                    edges[in_index].push_back(model.addVar(0.0, 1.0, 0.0, GRB_BINARY, label));
                }
            }

            carer_edges_[carer_index] = edges;
        }

        // define start times for visits
        for (const auto &node_item : node_visits_) {
            std::string label = "v_" + std::to_string(node_item.first) + "_start";
            visit_start_times_[node_item.first] = model.addVar(0.0, horizon_duration_.total_seconds(), 0.0,
                                                               GRB_CONTINUOUS, label);
        }

        // define active nodes for visits
        for (const auto &node_item : node_visits_) {
            std::string label = "v_" + std::to_string(node_item.first) + "_active";
            active_visits_[node_item.first] = model.addVar(0.0, 1.0, 0.0, GRB_BINARY, label);
        }

        // define start times for breaks
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto carer_break_node : carer_break_nodes_[carer_index]) {
                std::string label = "c_" + std::to_string(carer_index) + "_" + std::to_string(carer_break_node);
                carer_break_start_times_[carer_index].push_back(
                        model.addVar(0.0, horizon_duration_.total_seconds(), 0.0, GRB_CONTINUOUS, label));
            }
        }

        // 2 - all carers start their routes
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            GRBLinExpr flow_from_begin_depot = 0;
            for (auto to_node = first_visit_node_; to_node <= end_depot_node_; ++to_node) {
                flow_from_begin_depot += carer_edges_[carer_index][begin_depot_node_][to_node];
            }
            model.addConstr(flow_from_begin_depot == 1.0);
        }

        // >> initial depot gets zero inflow
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            // self loop is forbidden for all
            for (auto node_index = begin_depot_node_; node_index <= end_depot_node_; ++node_index) {
                model.addConstr(carer_edges_[carer_index][node_index][begin_depot_node_] == 0);
            }
        }

        // >> self loops are forbidden
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            // self loop is forbidden for all
            const auto num_carer_nodes = carer_edges_[carer_index].size();
            for (auto node_index = begin_depot_node_; node_index < num_carer_nodes; ++node_index) {
                model.addConstr(carer_edges_[carer_index][node_index][node_index] == 0);
            }
        }

        // 3 - all carers end their routes
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            GRBLinExpr flow_to_end_depot = 0;
            for (auto from_node = begin_depot_node_; from_node <= last_visit_node_; ++from_node) {
                flow_to_end_depot += carer_edges_[carer_index][from_node][end_depot_node_];
            }
            model.addConstr(flow_to_end_depot == 1.0);
        }

        // >> final depot get zero outflow
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto carer_num_nodes = carer_edges_[carer_index].size();
            for (auto to_node = begin_depot_node_; to_node < carer_num_nodes; ++to_node) {
                // traversal from the end depot is forbidden
                model.addConstr(carer_edges_[carer_index][end_depot_node_][to_node] == 0);
            }
        }

        // 4 - each visit is followed by travel to at most one node
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto in_index = first_visit_node_; in_index <= last_visit_node_; ++in_index) {
                GRBLinExpr node_outflow = 0;
                for (auto out_index = begin_depot_node_; out_index <= end_depot_node_; ++out_index) {
                    node_outflow += carer_edges_[carer_index][in_index][out_index];
                }
                model.addConstr(node_outflow <= 1.0);
            }
        }

        // 5 - flow conservation
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto carer_num_nodes = carer_edges_[carer_index].size();

            // for visits
            for (auto node_index = first_visit_node_; node_index < carer_num_nodes; ++node_index) {
                if (node_index == end_depot_node_) {
                    continue;
                }

                GRBLinExpr node_inflow = 0;
                GRBLinExpr node_outflow = 0;
                for (auto other_node_index = begin_depot_node_;
                     other_node_index <= last_visit_node_;
                     ++other_node_index) {
                    node_inflow += carer_edges_[carer_index][other_node_index][node_index];
                    node_outflow += carer_edges_[carer_index][node_index][other_node_index];
                }

                model.addConstr(node_inflow == node_outflow);
            }

            // for breaks
            for (auto node_index : carer_break_nodes_[carer_index]) {
                GRBLinExpr node_inflow = 0;
                GRBLinExpr node_outflow = 0;

                for (auto other_node_index = 0; other_node_index < carer_num_nodes; ++other_node_index) {
                    node_inflow += carer_edges_[carer_index][other_node_index][node_index];
                    node_outflow += carer_edges_[carer_index][node_index][other_node_index];
                }

                model.addConstr(node_inflow == node_outflow);
            }
        }

        // 6 - each break is taken exactly once
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto break_node : carer_break_nodes_[carer_index]) {
                GRBLinExpr node_inflow = 0;
                for (auto in_node = first_visit_node_; in_node <= last_visit_node_; ++in_node) {
                    node_inflow += carer_edges_[carer_index][in_node][break_node];
                }
                model.addConstr(node_inflow == 1.0);
            }
        }

        // 7 - return from break to the same node
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto break_node : carer_break_nodes_[carer_index]) {
                for (auto other_node = begin_depot_node_; other_node <= end_depot_node_; ++other_node) {
                    model.addConstr(carer_edges_[carer_index][break_node][other_node]
                                    == carer_edges_[carer_index][other_node][break_node]);
                }
            }
        }

        // 8 - carer taking break after a visit is scheduled to make that visit
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto break_node : carer_break_nodes_[carer_index]) {
                for (auto from_node = begin_depot_node_; from_node <= last_visit_node_; ++from_node) {
                    GRBLinExpr from_node_inflow = 0;
                    for (auto other_node = begin_depot_node_; other_node <= last_visit_node_; ++other_node) {
                        from_node_inflow += carer_edges_[carer_index][other_node][from_node];
                    }
                    model.addConstr(carer_edges_[carer_index][from_node][break_node] <= from_node_inflow);
                }
            }
        }

        // 9 - visit start times
        const auto BIG_M = horizon_duration_.total_seconds() + 3600;
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto from_node = first_visit_node_; from_node <= last_visit_node_; ++from_node) {
                for (auto to_node = first_visit_node_; to_node <= last_visit_node_; ++to_node) {
                    if (from_node == to_node) { continue; }

                    GRBLinExpr left = visit_start_times_[from_node]
                                      + node_visits_[from_node].duration().total_seconds()
                                      + location_container_.Distance(node_visits_[from_node].location().get(),
                                                                     node_visits_[to_node].location().get());
                    GRBLinExpr right = 0;
                    right += BIG_M * (1.0 - carer_edges_[carer_index][from_node][to_node]);
                    right += visit_start_times_[to_node];

                    model.addConstr(left <= right);
                }
            }
        }

        // 10 - break start times
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto num_breaks = carer_break_nodes_[carer_index].size();
            for (auto break_index = 0; break_index < num_breaks; ++break_index) {
                for (auto to_node = first_visit_node_; to_node <= last_visit_node_; ++to_node) {
                    GRBLinExpr left = carer_break_start_times_[carer_index][break_index]
                                      + carer_breaks_[carer_index][break_index +
                                                                   1].duration().total_seconds(); // normal breaks are offset by one
                    GRBLinExpr right = 0;
                    right += BIG_M * (1.0 - carer_edges_[carer_index][break_index][to_node]);
                    right += visit_start_times_[to_node];

                    model.addConstr(left <= right);
                }
            }
        }

        // 12 - active nodes
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto carer_nodes = carer_edges_[carer_index].size();
            for (auto visit_node = first_visit_node_; visit_node <= last_visit_node_; ++visit_node) {
                GRBLinExpr visit_inflow = 0;
                for (auto in_node = begin_depot_node_; in_node <= last_visit_node_; ++in_node) {
                    visit_inflow += carer_edges_[carer_index][in_node][visit_node];
                }
                model.addConstr(active_visits_[visit_node] == visit_inflow);
            }
        }

        // 13 - both nodes of a multiple carer visit are active
        for (const auto &visit_nodes : multiple_carer_visit_nodes_) {
            model.addConstr(active_visits_[visit_nodes.first] == active_visits_[visit_nodes.second]);
        }

        // 14 - both nodes of a multiple carer visit start at the same time
        for (const auto &visit_nodes : multiple_carer_visit_nodes_) {
            model.addConstr(visit_start_times_[visit_nodes.first] == visit_start_times_[visit_nodes.second]);
        }

        // 15 - start times for visits
        const auto time_window = boost::posix_time::minutes(90);
        for (const auto &visit_nodes : visit_start_times_) {
            GRBLinExpr left = active_visits_[visit_nodes.first] *
                              (node_visits_[visit_nodes.first].datetime().time_of_day() - time_window).total_seconds();

            GRBLinExpr right = active_visits_[visit_nodes.first] *
                               (node_visits_[visit_nodes.first].datetime().time_of_day() + time_window).total_seconds();

            model.addConstr(left <= visit_start_times_[visit_nodes.first]);
            model.addConstr(visit_start_times_[visit_nodes.first] <= right);
        }

        // 16 - start times for breaks
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto num_breaks = carer_break_nodes_[carer_index].size();
            for (auto break_index = 0; break_index < num_breaks; ++break_index) {
                const auto &break_ref = carer_breaks_[carer_index][break_index + 1];

                model.addConstr((break_ref.begin().time_of_day() - time_window).total_seconds() <=
                                carer_break_start_times_[carer_index][break_index]);
                model.addConstr(carer_break_start_times_[carer_index][break_index] <=
                                (break_ref.begin().time_of_day() + time_window).total_seconds());
            }
        }

        // define cost function
        // distance component
        GRBLinExpr cost = 0;
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto from_node = first_visit_node_; from_node <= last_visit_node_; ++from_node) {
                for (auto to_node = first_visit_node_; to_node <= last_visit_node_; ++to_node) {
                    const auto distance = location_container_.Distance(node_visits_[from_node].location().get(),
                                                                       node_visits_[to_node].location().get());
                    cost += distance * carer_edges_[carer_index][from_node][to_node];
                }
            }
        }

        const auto VISIT_NOT_SCHEDULED_PENALTY = horizon_duration_.total_seconds();

        // penalty for missing multiple carer visits
        for (const auto &visit_pair : multiple_carer_visit_nodes_) {
            cost += VISIT_NOT_SCHEDULED_PENALTY / 2.0
                    * (2.0 - active_visits_[visit_pair.first] - active_visits_[visit_pair.second]);
        }

        // penalty for missing single carer visits
        for (const auto &node_visit : node_visits_) {
            if (node_visit.second.carer_count() == 1) {
                cost += VISIT_NOT_SCHEDULED_PENALTY * (1.0 - active_visits_[node_visit.first]);
            }
        }

        model.setObjective(cost, GRB_MINIMIZE);
    }

    std::size_t first_visit_node_;
    std::size_t last_visit_node_;
    std::size_t begin_depot_node_;
    std::size_t end_depot_node_;
    std::size_t num_carers_;

    boost::posix_time::ptime horizon_start_;
    boost::posix_time::time_duration horizon_duration_;

    std::vector<rows::CalendarVisit> visits_;
    std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > carer_diaries_;
    rows::CachedLocationContainer location_container_;

    std::unordered_map<std::size_t, rows::CalendarVisit> node_visits_;
    std::vector<std::pair<std::size_t, std::size_t> > multiple_carer_visit_nodes_;
    std::unordered_map<std::size_t, GRBVar> visit_start_times_;
    std::unordered_map<std::size_t, GRBVar> active_visits_;

    std::vector<std::vector<rows::Event> > carer_breaks_;
    std::vector<std::vector<std::size_t> > carer_break_nodes_;
    std::vector<std::vector<GRBVar> > carer_break_start_times_;

    std::vector<std::vector<std::vector<GRBVar> > > carer_edges_;
};


int main(int argc, char *argv[]) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    const auto printer = util::CreatePrinter(util::TEXT_FORMAT);
    const auto problem = util::LoadProblem(FLAGS_problem, printer);
    const auto engine_config = util::CreateEngineConfig(FLAGS_maps);

    auto problem_model = Model::Create(problem, engine_config);
    problem_model.Solve();

    return 0;
}

void SaveCopy(rows::Problem problem, osrm::EngineConfig engine_config) {
    const auto carers = problem.carers();
    const auto num_carers = carers.size();

    const auto visits = problem.visits();

    std::vector<rows::Location> locations;
    for (const auto &visit :visits) {
        locations.push_back(visit.location().get());
    }
    rows::CachedLocationContainer location_container{std::cbegin(locations), std::cend(locations), engine_config};
    location_container.ComputeDistances();

    std::unordered_map<int, rows::CalendarVisit> node_visit;
    std::vector<std::pair<std::size_t, std::size_t> > multiple_carer_visit_nodes;

    auto current_node = 0;
    for (const auto &visit :visits) {
        node_visit[++current_node] = visit;
        if (visit.carer_count() == 2) {
            node_visit[++current_node] = visit;
            multiple_carer_visit_nodes.emplace_back(std::make_pair(current_node - 2, current_node - 1));
        }
    }

    const auto FIRST_VISIT_NODE = 1;
    const auto LAST_VISIT_NODE = current_node;
    const auto BEGIN_DEPOT = 0;
    const auto END_DEPOT = 1 + LAST_VISIT_NODE;

    boost::posix_time::ptime min_visit_start = boost::posix_time::max_date_time;
    for (const auto &visit : visits) {
        min_visit_start = std::min(min_visit_start, visit.datetime());
    }
    const auto start_horizon = min_visit_start.date();

    std::vector<std::vector<rows::Event> > carer_breaks(num_carers);
    std::vector<std::vector<std::size_t> > carer_break_nodes(num_carers);
    boost::posix_time::time_period time_horizon(
            boost::posix_time::ptime(start_horizon, boost::posix_time::time_duration()),
    boost::posix_time::seconds(rows::SolverWrapper::SECONDS_IN_DIMENSION));

    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        const auto &carer_pair = carers[carer_index];
        carer_breaks[carer_index] = carer_pair.second[0].Breaks(time_horizon);

        current_node = END_DEPOT;
        // first and last break of a carer denote out of office hours and those are not represented as nodes
        const auto FIRST_INNER_BREAK = 1;
        const auto LAST_INNER_BREAK = carer_breaks[carer_index].size() - 2;
        for (auto break_index = FIRST_INNER_BREAK; break_index <= LAST_INNER_BREAK; ++break_index) {
            carer_break_nodes[carer_index].push_back(++current_node);
        }
    }

    GRBEnv env = GRBEnv();
    GRBModel model = GRBModel(env);

    // define edges
    std::vector<std::vector<std::vector<GRBVar> > > carer_edges(num_carers);
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        // 2 depots plus all visit nodes (multiple carer visits are counted twice) plus breaks
        // the number of breaks depends on the carer
        const auto carer_num_nodes = 2 + node_visit.size() + carer_break_nodes[carer_index].size();

        std::vector<std::vector<GRBVar> > edges(carer_num_nodes, std::vector<GRBVar>());
        for (decltype(edges)::size_type in_index = 0; in_index < carer_num_nodes; ++in_index) {
            for (decltype(edges)::size_type out_index = 0; out_index < carer_num_nodes; ++out_index) {
                std::string label = "k_" + std::to_string(carer_index) + "_";
                if (in_index == BEGIN_DEPOT) {
                    label += "b";
                } else if (in_index == END_DEPOT) {
                    label += "e";
                } else if (in_index > END_DEPOT) {
                    label += "b" + std::to_string(in_index);
                } else {
                    label += "v" + std::to_string(in_index);
                }

                if (out_index == BEGIN_DEPOT) {
                    label += "b";
                } else if (out_index == END_DEPOT) {
                    label += "e";
                } else if (out_index > END_DEPOT) {
                    label += "b" + std::to_string(out_index);
                } else {
                    label += "v" + std::to_string(out_index);
                }

                edges[in_index].push_back(model.addVar(0.0, 1.0, 0.0, GRB_BINARY, label));
            }
        }

        carer_edges[carer_index] = edges;
    }

    // 2 - all carers start their routes
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        GRBLinExpr flow_from_begin_depot = 0;
        for (auto to_node = FIRST_VISIT_NODE; to_node <= END_DEPOT; ++to_node) {
            flow_from_begin_depot += carer_edges[carer_index][BEGIN_DEPOT][to_node];
        }
        model.addConstr(flow_from_begin_depot == 1.0);
    }

    // >> initial depot gets zero inflow
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        // self loop is forbidden for all
        for (auto node_index = BEGIN_DEPOT; node_index <= END_DEPOT; ++node_index) {
            model.addConstr(carer_edges[carer_index][node_index][BEGIN_DEPOT] == 0);
        }
    }

    // >> self loops are forbidden
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        // self loop is forbidden for all
        const auto num_carer_nodes = carer_edges[carer_index].size();
        for (auto node_index = BEGIN_DEPOT; node_index < num_carer_nodes; ++node_index) {
            model.addConstr(carer_edges[carer_index][node_index][node_index] == 0);
        }
    }

    // 3 - all carers end their routes
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        GRBLinExpr flow_to_end_depot = 0;
        for (auto from_node = BEGIN_DEPOT; from_node <= LAST_VISIT_NODE; ++from_node) {
            flow_to_end_depot += carer_edges[carer_index][from_node][END_DEPOT];
        }
        model.addConstr(flow_to_end_depot == 1.0);
    }

    // >> final depot get zero outflow
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        const auto carer_num_nodes = carer_edges[carer_index].size();
        for (auto to_node = BEGIN_DEPOT; to_node < carer_num_nodes; ++to_node) {
            // traversal from the end depot is forbidden
            model.addConstr(carer_edges[carer_index][END_DEPOT][to_node] == 0);
        }
    }

    // 4 - each visit is followed by travel to at most one node
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        for (auto in_index = FIRST_VISIT_NODE; in_index <= LAST_VISIT_NODE; ++in_index) {
            GRBLinExpr node_outflow = 0;
            for (auto out_index = BEGIN_DEPOT; out_index <= END_DEPOT; ++out_index) {
                node_outflow += carer_edges[carer_index][in_index][out_index];
            }
            model.addConstr(node_outflow <= 1.0);
        }
    }

    // 5 - flow conservation
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        const auto carer_num_nodes = carer_edges[carer_index].size();

        // for visits
        for (auto node_index = FIRST_VISIT_NODE; node_index < carer_num_nodes; ++node_index) {
            if (node_index == END_DEPOT) {
                continue;
            }

            GRBLinExpr node_inflow = 0;
            GRBLinExpr node_outflow = 0;

            for (auto other_node_index = BEGIN_DEPOT; other_node_index <= LAST_VISIT_NODE; ++other_node_index) {
                node_inflow += carer_edges[carer_index][other_node_index][node_index];
                node_outflow += carer_edges[carer_index][node_index][other_node_index];
            }

            model.addConstr(node_inflow == node_outflow);
        }

        // for breaks
        for (auto node_index : carer_break_nodes[carer_index]) {
            GRBLinExpr node_inflow = 0;
            GRBLinExpr node_outflow = 0;

            for (auto other_node_index = 0; other_node_index < carer_num_nodes; ++other_node_index) {
                node_inflow += carer_edges[carer_index][other_node_index][node_index];
                node_outflow += carer_edges[carer_index][node_index][other_node_index];
            }

            model.addConstr(node_inflow == node_outflow);
        }
    }

    // 6 - each break is taken exactly once
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        for (auto break_node : carer_break_nodes[carer_index]) {
            GRBLinExpr node_inflow = 0;
            for (auto in_node = BEGIN_DEPOT; in_node <= LAST_VISIT_NODE; ++in_node) {
                node_inflow += carer_edges[carer_index][in_node][break_node];
            }
            model.addConstr(node_inflow == 1.0);
        }
    }

    // 7 - return from break to the same node
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        for (auto break_node : carer_break_nodes[carer_index]) {
            for (auto other_node = BEGIN_DEPOT; other_node <= END_DEPOT; ++other_node) {
                model.addConstr(carer_edges[carer_index][break_node][other_node]
                                == carer_edges[carer_index][other_node][break_node]);
            }
        }
    }

    // 8 - carer taking break after a visit is scheduled to make that visit
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        for (auto break_node : carer_break_nodes[carer_index]) {
            for (auto from_node = BEGIN_DEPOT; from_node <= LAST_VISIT_NODE; ++from_node) {
                GRBLinExpr from_node_inflow = 0;
                for (auto other_node = BEGIN_DEPOT; other_node <= LAST_VISIT_NODE; ++other_node) {
                    from_node_inflow += carer_edges[carer_index][other_node][from_node];
                }
                model.addConstr(carer_edges[carer_index][from_node][break_node] <= from_node_inflow);
            }
        }
    }

    // define cost function
    GRBLinExpr cost = 0;
    for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
        for (auto from_node = FIRST_VISIT_NODE; from_node <= LAST_VISIT_NODE; ++from_node) {
            for (auto to_node = FIRST_VISIT_NODE; to_node <= LAST_VISIT_NODE; ++to_node) {
                const auto distance = location_container.Distance(node_visit[from_node].location().get(),
                                                                  node_visit[to_node].location().get());
                cost += distance * carer_edges[carer_index][from_node][to_node];
            }
        }
    }

    model.setObjective(cost, GRB_MINIMIZE);

    // v.set(GRB_DoubleAttr_Start, value);

    model.set(GRB_DoubleParam_TimeLimit, 60 * 5);
    model.set(GRB_IntParam_Presolve, 2); // max 2
//    model.set(GRB_DoubleParam_Heuristics, 0.2);
    model.set(GRB_IntParam_MIPFocus, 1); // 1 - focus on feasible solutions, 2 - focus on proving optimality, 3 - focus on bound

    model.optimize();

    const auto solver_status = model.get(GRB_IntAttr_Status);
    LOG(INFO) << "Status " << GetStatus(solver_status);

    if (solver_status == GRB_OPTIMAL) {
        std::vector<std::vector<decltype(carer_edges)::size_type> > carer_paths;

        static const auto BLANK_NODE = -1;

        for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
            const auto carer_num_nodes = carer_edges[carer_index].size();
            std::vector<int> next_visit_nodes(carer_num_nodes, BLANK_NODE);
            std::vector<std::vector<int> > next_break_nodes(carer_num_nodes);

            for (auto from_node = BEGIN_DEPOT; from_node <= END_DEPOT; ++from_node) {
                for (auto to_node = BEGIN_DEPOT; to_node < carer_num_nodes; ++to_node) {
                    const auto value = carer_edges[carer_index][from_node][to_node].get(GRB_DoubleAttr_X);
                    CHECK(value == 0.0 || value == 1.0);
                    if (value == 1.0) {
                        if (to_node <= END_DEPOT) { // handle visit
                            if (from_node <= END_DEPOT) { // coming back from prior breaks is ignored
                                CHECK_EQ(next_visit_nodes.at(from_node), BLANK_NODE) << " at " << to_node;
//                            if (next_visit_nodes.at(from_node) != BLANK_NODE) {
//
//                                for (auto local_to_node = BEGIN_DEPOT; local_to_node < carer_num_nodes; ++local_to_node) {
//                                    LOG(INFO) << from_node << " : " << local_to_node << " : " << carer_edges[carer_index][from_node][local_to_node].get(GRB_DoubleAttr_X);
//                                }
//
//                                CHECK(false);
//                            }
                                next_visit_nodes.at(from_node) = to_node;
                            }
                        } else { // handle break
                            next_break_nodes.at(from_node).push_back(to_node);
                        }
                    }
                }
            }

            std::vector<decltype(carer_edges)::size_type> carer_path;
            auto current_visit_node = BEGIN_DEPOT;
            while (current_visit_node != BLANK_NODE) {
                carer_path.push_back(current_visit_node);

                for (auto break_index : next_break_nodes[current_visit_node]) {
                    carer_path.push_back(break_index);
                }

                const auto next_visit_node = next_visit_nodes[current_visit_node];

                CHECK_NE(current_visit_node, next_visit_node) << "Current node (" << current_visit_node
                                                              << ") must not be equal to the next node ("
                                                              << next_visit_node << ")";
                current_visit_node = next_visit_node;
            }

            carer_paths.emplace_back(std::move(carer_path));
        }

        std::stringstream output_msg;
        for (auto carer_index = 0; carer_index < num_carers; ++carer_index) {
            output_msg << "Carer " << carer_index << ": ";

            auto &carer_path = carer_paths[carer_index];

            CHECK(!carer_path.empty());
            output_msg << carer_path[0];
            const auto carer_path_size = carer_path.size();
            for (auto node_index = 1; node_index < carer_path_size; ++node_index) {
                output_msg << " -> " << carer_path[node_index];
            }

            output_msg << std::endl;
        }

        LOG(INFO) << output_msg.str();
    }
}
