#include <utility>

#include <sstream>

#include <gurobi_c++.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <boost/config.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/algorithm/string/join.hpp>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/routing_parameters.pb.h>

#include "util/input.h"
#include "util/logging.h"
#include "util/validation.h"

#include "location_container.h"
#include "solver_wrapper.h"
#include "break.h"
#include "single_step_solver.h"
#include "second_step_solver.h"
#include "gexf_writer.h"

DEFINE_string(problem, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists);

DEFINE_string(solution, "", "a file path to the solution file for warm start");
DEFINE_validator(solution, &util::file::IsNullOrExists);

DEFINE_string(maps, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(maps, &util::file::Exists);

DEFINE_string(output, "output.gexf", "an output file");

static const auto DEFAULT_TIME_LIMIT_TEXT = "00:03:00";
static const auto DEFAULT_TIME_LIMIT = boost::posix_time::duration_from_string(DEFAULT_TIME_LIMIT_TEXT);
DEFINE_string(time_limit, DEFAULT_TIME_LIMIT_TEXT, "time limit for proving the optimality");

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
        case GRB_TIME_LIMIT:
            return "TIME_LIMIT";
        default:
            return "UNKNOWN";
    }
}

std::vector<rows::Location> getLocations(const rows::Problem &problem) {
    std::vector<rows::Location> locations;
    for (const auto &visit : problem.visits()) {
        locations.push_back(visit.location().get());
    }
    return locations;
}


struct Activity {
public:
    static Activity CreateTravel(boost::posix_time::ptime datetime, boost::posix_time::time_duration duration) {
        return {0, "travel", datetime, std::move(duration), boost::posix_time::time_duration()};
    }

    static Activity CreateBreak(std::size_t break_node,
                                boost::posix_time::ptime datetime,
                                boost::posix_time::time_duration duration,
                                boost::posix_time::time_duration waiting_time) {
        return {break_node, "break", datetime, std::move(duration), std::move(waiting_time)};
    }

    static Activity CreateVisit(std::size_t visit_node,
                                boost::posix_time::ptime datetime,
                                boost::posix_time::time_duration duration,
                                boost::posix_time::time_duration waiting_time) {
        return {visit_node, "visit", datetime, std::move(duration), std::move(waiting_time)};
    }

    Activity(std::size_t node,
             std::string type,
             boost::posix_time::ptime datetime,
             boost::posix_time::time_duration duration,
             boost::posix_time::time_duration waiting_time)
            : node_{node},
              type_(std::move(type)),
              datetime_(datetime),
              duration_(std::move(duration)),
              waiting_time_(std::move(waiting_time)) {}

    const std::size_t node() const { return node_; }

    const std::string &type() const { return type_; }

    const boost::posix_time::ptime &datetime() const { return datetime_; }

    const boost::posix_time::time_duration &duration() const { return duration_; }

    const boost::posix_time::time_duration &waiting_time() const { return waiting_time_; }

private:
    std::size_t node_;
    std::string type_;
    boost::posix_time::ptime datetime_;
    boost::posix_time::time_duration duration_;
    boost::posix_time::time_duration waiting_time_;
};

std::ostream &operator<<(std::ostream &out, const Activity &activity) {
    out << boost::format("%1% %4t%2% %|12t|%3% %|32t|(%4%) %|46t|%5%")
           % activity.node()
           % activity.type()
           % activity.datetime()
           % activity.waiting_time()
           % activity.duration();
    return out;
}

class Model {
public:
    static Model Create(const rows::Problem &problem,
                        osrm::EngineConfig engine_config,
                        boost::posix_time::time_duration visit_time_window,
                        boost::posix_time::time_duration break_time_window,
                        boost::posix_time::time_duration overtime_allowance) {

        std::vector<rows::Location> locations;
        for (const auto &visit : problem.visits()) {
            locations.push_back(visit.location().get());
        }

        rows::CachedLocationContainer location_container(std::cbegin(locations), std::cend(locations), engine_config);
        location_container.ComputeDistances();

        return Model{problem,
                     std::move(location_container),
                     std::move(visit_time_window),
                     std::move(break_time_window),
                     std::move(overtime_allowance)};
    }

    Model(const rows::Problem &problem,
          rows::CachedLocationContainer location_container,
          boost::posix_time::time_duration visit_time_window,
          boost::posix_time::time_duration break_time_window,
          boost::posix_time::time_duration overtime_allowance)
            : visits_(problem.visits()),
              carer_diaries_(problem.carers()),
              location_container_(std::move(location_container)),
              node_visits_{},
              multiple_carer_visit_nodes_{},
              begin_depot_node_{0},
              first_visit_node_{1},
              visit_time_window_{std::move(visit_time_window)},
              break_time_window_{std::move(break_time_window)},
              overtime_window_{std::move(overtime_allowance)} {
        num_carers_ = carer_diaries_.size();
        carer_node_breaks_.resize(num_carers_);
        carer_break_start_times_.resize(num_carers_);
        carer_break_potentials_.resize(num_carers_);
        carer_edges_.resize(num_carers_);

        auto current_node = 0;
        for (const auto &visit :visits_) {
            node_visits_[++current_node] = visit;
            if (visit.carer_count() == 2) {
                node_visits_[++current_node] = visit;
                CHECK(visit.location());
                multiple_carer_visit_nodes_.emplace_back(std::make_pair(current_node - 1, current_node));
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
            // reset current node so all breaks start from the same node
            current_node = end_depot_node_;
            const auto &carer_pair = carer_diaries_[carer_index];
            const auto &carer_diary = carer_pair.second[0];
            const auto carer_breaks = carer_diary.Breaks(time_horizon);
            if (carer_breaks.empty()) { continue; }

            const auto last_break_index = carer_breaks.size() - 1;
            const auto &first_break = carer_breaks[0];
            rows::Break first_break_to_use{
                    carer_pair.first,
                    first_break.begin(),
                    first_break.duration() - overtime_window_};
            carer_node_breaks_[carer_index].emplace(++current_node, std::move(first_break_to_use));

            for (auto break_index = 1; break_index < last_break_index; ++break_index) {
                const auto &break_item = carer_breaks[break_index];
                rows::Break break_to_use{carer_pair.first, break_item.begin(), break_item.duration()};
                carer_node_breaks_[carer_index].emplace(++current_node, std::move(break_to_use));
            }

            const auto &last_break = carer_breaks[last_break_index];
            rows::Break last_break_to_use{carer_pair.first,
                                          last_break.begin() + overtime_window_,
                                          last_break.duration() - overtime_window_};
            carer_node_breaks_[carer_index].emplace(++current_node, std::move(last_break_to_use));
        }
    }

    rows::Solution Solve(const boost::optional<rows::Solution> &initial_solution,
                         const boost::posix_time::time_duration &time_limit) {
        GRBEnv env = GRBEnv();
        GRBModel model = GRBModel(env);

        Build(model, initial_solution);

        model.set(GRB_DoubleParam_TimeLimit, time_limit.total_seconds());
        model.set(GRB_IntParam_Presolve, 2); // max 2
        // model.set(GRB_DoubleParam_Heuristics, 0.2);
        // 1 - focus on feasible solutions, 2 - focus on proving optimality, 3 - focus on bound
        model.set(GRB_IntParam_MIPFocus, 2);
//        model.set(GRB_IntParam_SubMIPNodes, GRB_MAXINT); // set to max int if the initial solution is partial
        model.optimize();

        const auto solver_status = model.get(GRB_IntAttr_Status);


        if (solver_status != GRB_OPTIMAL && solver_status != GRB_TIME_LIMIT) {
//            model.computeIIS();
//            model.write("failed_model.ilp");
            LOG(FATAL) << "Invalid status: " << GetStatus(solver_status);
            return rows::Solution();
        }

        std::vector<rows::ScheduledVisit> visits;
        std::vector<rows::Break> breaks;

        std::vector<std::vector<decltype(carer_edges_)::size_type>> carer_paths;
        static const auto BLANK_NODE = -1;
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto carer_num_nodes = carer_edges_[carer_index].size();
            std::vector<int> next_visit_nodes(carer_num_nodes, BLANK_NODE);
            std::vector<std::vector<int>> next_break_nodes(carer_num_nodes);

            for (auto from_node = begin_depot_node_; from_node <= end_depot_node_; ++from_node) {
                for (auto to_node = begin_depot_node_; to_node < carer_num_nodes; ++to_node) {
                    const auto value = carer_edges_[carer_index][from_node][to_node].get(GRB_DoubleAttr_X);
                    CHECK(value == 0.0 || value == 1.0);
                    if (value == 1.0) {
                        if (to_node <= end_depot_node_) { // handle visit
                            if (from_node <= end_depot_node_) { // coming back from prior breaks is ignored
                                CHECK_EQ(next_visit_nodes.at(from_node), BLANK_NODE) << " at " << to_node;
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
                for (auto break_index : next_break_nodes[current_visit_node]) {
                    carer_path.push_back(break_index);
                }
                carer_path.push_back(current_visit_node);

                const auto next_visit_node = next_visit_nodes[current_visit_node];
                CHECK_NE(current_visit_node, next_visit_node) << "Current node (" << current_visit_node
                                                              << ") must not be equal to the next node ("
                                                              << next_visit_node << ")";
                current_visit_node = next_visit_node;
            }

            const auto &carer_ref = carer_diaries_[carer_index].first;
            for (const auto visit_node : carer_path) {
                if (visit_node == begin_depot_node_ || visit_node >= end_depot_node_) {
                    continue;
                }

                CHECK(node_visits_[visit_node].location());
                const auto &visit_ref = node_visits_[visit_node];
                boost::posix_time::ptime visit_start_time{
                        visit_ref.datetime().date(),
                        boost::posix_time::seconds(
                                static_cast<int>(visit_start_times_[visit_node].get(GRB_DoubleAttr_X)))};
                visits.emplace_back(rows::ScheduledVisit::VisitType::OK,
                                    carer_ref,
                                    visit_start_time,
                                    visit_ref.duration(),
                                    boost::none,
                                    boost::none,
                                    visit_ref);
            }

            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                const auto seconds = carer_break_start_times_[carer_index][break_item.first].get(GRB_DoubleAttr_X);
                boost::posix_time::ptime start_time{horizon_start_.date(),
                                                    boost::posix_time::seconds(static_cast<int>(seconds))};
                breaks.emplace_back(carer_ref, start_time, break_item.second.duration());
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

        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            std::vector<std::size_t> visit_nodes;
            const auto carer_nodes = carer_edges_[carer_index].size();
            for (auto from_node = begin_depot_node_; from_node < end_depot_node_; ++from_node) {
                for (auto to_node = begin_depot_node_; to_node < end_depot_node_; ++to_node) {
                    if (carer_edges_[carer_index][from_node][to_node].get(GRB_DoubleAttr_X) == 1.0) {
                        visit_nodes.push_back(to_node);
                    }
                }
            }
        }

        const auto solution = rows::Solution(std::move(visits), std::move(breaks));

        Print(solution);

        return solution;
    }

    void Print(const rows::Solution &solution) {

        std::vector<std::vector<std::pair<std::size_t, std::size_t> > > marked_edges{num_carers_};
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto carer_num_nodes = carer_edges_[carer_index].size();
            for (auto from_index = 0; from_index < carer_num_nodes; ++from_index) {
                for (auto to_index = 0; to_index < carer_num_nodes; ++to_index) {
                    if (carer_edges_[carer_index][from_index][to_index].get(GRB_DoubleAttr_X) == 1.0) {
                        marked_edges[carer_index].emplace_back(from_index, to_index);
                    }
                }
            }
        }

        for (const auto &carer : solution.Carers()) {
            const auto carer_index = GetIndex(carer);

            const auto get_visit_node = [&carer_index, &marked_edges, this](
                    const rows::CalendarVisit &visit) -> std::size_t const {
                const auto visit_nodes = this->GetNodes(visit);
                for (const auto visit_node : visit_nodes) {
                    for (const auto edge : marked_edges[carer_index]) {
                        if (edge.first == visit_node || edge.second == visit_node) {
                            return visit_node;
                        }
                    }
                }

                LOG(FATAL) << "Failed to find the visit node";
            };


            std::vector<rows::ScheduledVisit> carer_visits;
            for (const auto &visit : solution.visits()) {
                const auto &visit_carer_opt = visit.carer();
                if (visit_carer_opt.get() == carer) {
                    carer_visits.push_back(visit);
                }
            }
            std::sort(std::begin(carer_visits),
                      std::end(carer_visits),
                      [](const rows::ScheduledVisit &left, const rows::ScheduledVisit &right) -> bool {
                          return left.datetime() <= right.datetime();
                      });

            std::vector<rows::Break> carer_breaks;
            for (const auto &break_element : solution.breaks()) {
                if (break_element.carer() == carer) {
                    carer_breaks.push_back(break_element);
                }
            }
            std::sort(std::begin(carer_breaks), std::end(carer_breaks),
                      [](const rows::Break &left, const rows::Break &right) -> bool {
                          return left.datetime() <= right.datetime();
                      });

            auto break_it = std::begin(carer_breaks);
            const auto break_end_it = std::end(carer_breaks);
            CHECK(break_it != break_end_it);

            auto visit_it = std::begin(carer_visits);
            const auto visit_end_it = std::end(carer_visits);

            boost::posix_time::ptime current_time = break_it->datetime();
            if (visit_it != visit_end_it) {
                current_time = std::min(current_time, visit_it->datetime());
            }

            std::vector<Activity> activities;
            boost::optional<rows::Location> current_location = boost::none;
            while (break_it != break_end_it || visit_it != visit_end_it) {
                if (break_it != break_end_it && visit_it != visit_end_it) {
                    CHECK(visit_it->location());

                    boost::posix_time::ptime arrival_time = current_time;
                    boost::posix_time::time_duration travel_duration;
                    if (current_location && current_location != visit_it->location()) {
                        travel_duration = boost::posix_time::seconds(
                                location_container_.Distance(current_location.get(), visit_it->location().get()));
                        arrival_time += travel_duration;
                    }

                    if (break_it->datetime() <= visit_it->datetime()) {
                        if (arrival_time <= break_it->datetime()) {
                            if (current_location && current_location != visit_it->location()) {
                                activities.emplace_back(Activity::CreateTravel(current_time, travel_duration));
                            }
                            current_location = visit_it->location().get();
                            current_time = arrival_time;
                        }

                        const auto waiting_time = break_it->datetime() - current_time;
                        const auto break_node = GetNodeOrNeighbor(carer_index, *break_it);
                        activities.emplace_back(
                                Activity::CreateBreak(break_node,
                                                      break_it->datetime(),
                                                      break_it->duration(),
                                                      waiting_time));

                        current_time = break_it->datetime() + break_it->duration();
                        ++break_it;
                    } else {
                        if (current_location && current_location != visit_it->location()) {
                            activities.emplace_back(Activity::CreateTravel(current_time, travel_duration));
                        }
                        current_location = visit_it->location().get();
                        current_time = arrival_time;

                        const auto waiting_time = visit_it->datetime() - current_time;
                        const auto visit_node = get_visit_node(visit_it->calendar_visit().get());
                        activities.emplace_back(Activity::CreateVisit(visit_node,
                                                                      visit_it->datetime(),
                                                                      visit_it->duration(),
                                                                      waiting_time));
                        current_time = visit_it->datetime() + visit_it->duration();
                        ++visit_it;
                    }
                } else if (break_it != break_end_it) {
                    const auto waiting_time = break_it->datetime() - current_time;
                    const auto break_node = GetNodeOrNeighbor(carer_index, *break_it);
                    activities.emplace_back(Activity::CreateBreak(break_node,
                                                                  break_it->datetime(),
                                                                  break_it->duration(),
                                                                  waiting_time));
                    current_time = break_it->datetime() + break_it->duration();
                    ++break_it;
                } else {
                    if (current_location && current_location != visit_it->location()) {
                        const auto travel_duration = boost::posix_time::seconds(
                                location_container_.Distance(current_location.get(), visit_it->location().get()));
                        if (current_location) {
                            activities.emplace_back(Activity::CreateTravel(current_time, travel_duration));
                        }
                        current_time += travel_duration;
                    }
                    current_location = visit_it->location().get();

                    const auto waiting_time = visit_it->datetime() - current_time;
                    const auto visit_node = get_visit_node(visit_it->calendar_visit().get());
                    activities.emplace_back(Activity::CreateVisit(visit_node,
                                                                  visit_it->datetime(),
                                                                  visit_it->duration(),
                                                                  waiting_time));
                    current_time = visit_it->datetime() + visit_it->duration();
                    ++visit_it;
                }
            }

            LOG(INFO) << "Carer " << carer;
            LOG(INFO) << carer_diaries_[carer_index].first;
            for (const auto &edge : marked_edges[carer_index]) {
                auto potential = edge.second + 1;
                if (edge.second > end_depot_node_) {
                    potential = carer_break_potentials_[carer_index][edge.second].get(GRB_DoubleAttr_X);
                }

                LOG(INFO) << boost::format("%|4t|%1% %|8t|%2% %|12t|%3% %|16t|(%4%)")
                             % carer_index
                             % edge.first
                             % edge.second
                             % potential;
            }

            for (const auto &activity : activities) {
                LOG(INFO) << activity;
            }
        }
    }

    void PrintStats() const {
        for (const auto &visit_item : node_visits_) {
            LOG(INFO) << visit_item.first << " " << visit_item.second;
        }

        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            LOG(INFO) << carer_diaries_[carer_index].first;

            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                LOG(INFO) << break_item.second;
            }
        }
    }

    rows::CachedLocationContainer &LocationContainer() { return location_container_; }

private:
    void Build(GRBModel &model, const boost::optional<rows::Solution> &solution_opt) {
        // define edges
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            // 2 depots plus all visit nodes (multiple carer visits are counted twice) plus breaks
            // the number of breaks depends on the carer
            const auto carer_num_nodes = 2 + node_visits_.size() + carer_node_breaks_[carer_index].size();

            std::vector<std::vector<GRBVar>> edges(carer_num_nodes, std::vector<GRBVar>());
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
            for (const auto &carer_break_item : carer_node_breaks_[carer_index]) {
                std::string label = "c_" + std::to_string(carer_index) + "_" + std::to_string(carer_break_item.first);
                carer_break_start_times_[carer_index].emplace(
                        carer_break_item.first,
                        model.addVar(0.0, horizon_duration_.total_seconds(), 0.0, GRB_CONTINUOUS, label));
            }
        }

        // define potentials for breaks
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto num_carer_nodes = carer_edges_[carer_index].size();
            for (const auto &carer_break_item : carer_node_breaks_[carer_index]) {
                std::string label = "c_" + std::to_string(carer_index) + "_" + std::to_string(carer_break_item.first) +
                                    "_potential";
                carer_break_potentials_[carer_index].emplace(
                        carer_break_item.first,
                        model.addVar(1.0, num_carer_nodes + 1, 0.0, GRB_CONTINUOUS, label));
            }
        }

        // >> define start and end times for depots
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            std::string begin_label = "d_" + std::to_string(carer_index) + "_start";
            begin_depot_start_.push_back(
                    model.addVar(0.0, horizon_duration_.total_seconds(), 0.0, GRB_CONTINUOUS, begin_label));

            std::string end_label = "d_" + std::to_string(carer_index) + "_end";
            end_depot_start_.push_back(
                    model.addVar(0.0, horizon_duration_.total_seconds(), 0.0, GRB_CONTINUOUS, end_label));
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
            for (auto to_node = begin_depot_node_; to_node <= last_visit_node_; ++to_node) {
                // traversal from the end depot is forbidden
                model.addConstr(carer_edges_[carer_index][end_depot_node_][to_node] == 0);
            }
        }

        // 4 - each visit is followed by travel to at most one node
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto in_index = first_visit_node_; in_index <= last_visit_node_; ++in_index) {
                GRBLinExpr node_outflow = 0;
                for (auto out_index = first_visit_node_; out_index <= end_depot_node_; ++out_index) {
                    node_outflow += carer_edges_[carer_index][in_index][out_index];
                }
                model.addConstr(node_outflow <= 1.0);
            }
        }

        // >> each visit is performed at most once
        for (auto visit_node = first_visit_node_; visit_node <= last_visit_node_; ++visit_node) {
            GRBLinExpr node_inflow = 0;
            for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
                for (auto prev_node = 0; prev_node <= last_visit_node_; ++prev_node) {
                    node_inflow += carer_edges_[carer_index][prev_node][visit_node];
                }
            }
            model.addConstr(node_inflow <= 1.0);
        }

        // 5 - flow conservation
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto carer_num_nodes = carer_edges_[carer_index].size();

            // for visits
            for (auto node_index = first_visit_node_; node_index <= last_visit_node_; ++node_index) {
                GRBLinExpr node_inflow = 0;
                GRBLinExpr node_outflow = 0;
                for (auto other_node_index = 0; other_node_index < carer_num_nodes; ++other_node_index) {
                    node_inflow += carer_edges_[carer_index][other_node_index][node_index];
                    node_outflow += carer_edges_[carer_index][node_index][other_node_index];
                }

                model.addConstr(node_inflow == node_outflow);
            }

            // for breaks
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                const auto node_index = break_item.first;

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
            const auto num_carer_nodes = carer_edges_[carer_index].size();
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                const auto break_node = break_item.first;

                GRBLinExpr node_inflow = 0;
                for (auto in_node = begin_depot_node_; in_node < num_carer_nodes; ++in_node) {
                    node_inflow += carer_edges_[carer_index][in_node][break_node];
                }
                model.addConstr(node_inflow == 1.0);
            }
        }

        // 7 - return from break to the same node
//        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
//            for (const auto &break_item : carer_node_breaks_[carer_index]) {
//                const auto break_node = break_item.first;
//
//                for (auto other_node = begin_depot_node_; other_node <= end_depot_node_; ++other_node) {
//                    model.addConstr(carer_edges_[carer_index][break_node][other_node]
//                                    == carer_edges_[carer_index][other_node][break_node]);
//                }
//            }
//        }

        // 8 - carer taking break before a visit is scheduled to make that visit
        // cases for the begin and end depot are trivially satisfied
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                const auto break_node = break_item.first;

                for (auto visit_node = first_visit_node_; visit_node <= last_visit_node_; ++visit_node) {
                    GRBLinExpr visit_node_inflow = 0;
                    for (auto from_node = begin_depot_node_; from_node <= end_depot_node_; ++from_node) {
                        visit_node_inflow += carer_edges_[carer_index][from_node][visit_node];
                    }
                    model.addConstr(carer_edges_[carer_index][visit_node][break_node] <= visit_node_inflow);
                    model.addConstr(carer_edges_[carer_index][break_node][visit_node] <= visit_node_inflow);
                }
            }
        }

        const auto BIG_M = horizon_duration_.total_seconds();

        // >> begin start time is lower or equal to the first visits
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto visit_node = first_visit_node_; visit_node <= last_visit_node_; ++visit_node) {
                model.addConstr(begin_depot_start_[carer_index]
                                <= visit_start_times_[visit_node] +
                                   BIG_M * (1 - carer_edges_[carer_index][begin_depot_node_][visit_node]));
            }
        }

        // >> begin start time is greater than the break at begin depot
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                model.addConstr( // break_item.second.datetime().time_of_day().total_seconds() +
                        break_item.second.duration().total_seconds()
                        <= begin_depot_start_[carer_index]
                           + BIG_M * (1 - carer_edges_[carer_index][break_item.first][begin_depot_node_]));
            }
        }

        // >> end time is greater or equal the finish of the last visit
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto visit_node = first_visit_node_; visit_node <= last_visit_node_; ++visit_node) {
                model.addConstr(visit_start_times_[visit_node]
                                + node_visits_[visit_node].duration().total_seconds()
                                - BIG_M
                                + BIG_M * carer_edges_[carer_index][visit_node][end_depot_node_]
                                <= end_depot_start_[carer_index]);
            }
        }

        // >> last break is after end time
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto &break_item : carer_node_breaks_[carer_index]) {
                model.addConstr(end_depot_start_[carer_index]
                                - BIG_M
                                + BIG_M * carer_edges_[carer_index][break_item.first][end_depot_node_]
                                <= carer_break_start_times_[carer_index][break_item.first]);
            }
        }

        // 9 - visit start times
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (auto from_node = first_visit_node_; from_node <= last_visit_node_; ++from_node) {
                for (auto to_node = first_visit_node_; to_node <= last_visit_node_; ++to_node) {
                    if (from_node == to_node) { continue; }

                    model.addConstr(visit_start_times_[from_node]
                                    + node_visits_[from_node].duration().total_seconds()
                                    + location_container_.Distance(node_visits_[from_node].location().get(),
                                                                   node_visits_[to_node].location().get())
                                    <= visit_start_times_[to_node]
                                       + BIG_M * (1 - carer_edges_[carer_index][from_node][to_node]));
                }
            }
        }

        // 10 - visit start times after break
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                const auto break_node = break_item.first;
                for (auto to_node = first_visit_node_; to_node <= last_visit_node_; ++to_node) {
                    GRBLinExpr left = carer_break_start_times_[carer_index][break_node]
                                      + break_item.second.duration().total_seconds();
                    GRBLinExpr right = 0;
                    right += BIG_M * (1.0 - carer_edges_[carer_index][break_node][to_node]);
                    right += visit_start_times_[to_node];

                    model.addConstr(left <= right);
                }
            }
        }

        // >> break start times after the begin depot
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                for (auto next_visit_node = first_visit_node_; next_visit_node <= last_visit_node_; ++next_visit_node) {
                    GRBLinExpr right = 0;
                    right += carer_break_start_times_[carer_index][break_item.first];
                    right += BIG_M * (2.0
                                      - carer_edges_[carer_index][begin_depot_node_][next_visit_node]
                                      - carer_edges_[carer_index][next_visit_node][break_item.first]);
                    model.addConstr(begin_depot_start_[carer_index] <= right);
                }
            }
        }

        // >> break start times
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                for (auto prev_visit_node = first_visit_node_; prev_visit_node <= last_visit_node_; ++prev_visit_node) {
                    for (auto next_visit_node = first_visit_node_;
                         next_visit_node <= last_visit_node_; ++next_visit_node) {
                        GRBLinExpr left = 0;
                        left += visit_start_times_[prev_visit_node];
                        left += node_visits_[prev_visit_node].duration().total_seconds();

                        GRBLinExpr right = 0;
                        right += carer_break_start_times_[carer_index][break_item.first];
                        right += BIG_M * (2.0
                                          - carer_edges_[carer_index][prev_visit_node][next_visit_node]
                                          - carer_edges_[carer_index][next_visit_node][break_item.first]);
                        model.addConstr(left <= right);
                    }
                }
            }
        }

        // >> at most one break can be connected to a visit
        for (auto visit_node = first_visit_node_; visit_node <= last_visit_node_; ++visit_node) {
            GRBLinExpr break_inflow = 0;
            for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
                for (const auto &break_item : carer_node_breaks_[carer_index]) {
                    break_inflow += carer_edges_[carer_index][break_item.first][visit_node];
                }
            }
            model.addConstr(break_inflow <= 1.0);
        }

        // >> one break can be connected to a begin depot
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            GRBLinExpr break_inflow = 0;
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                break_inflow += carer_edges_[carer_index][begin_depot_node_][break_item.first];
            }
            model.addConstr(break_inflow == 1.0);
        }

        // >> connections between breaks are not allowed
//        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
//            for (const auto &in_break_item : carer_node_breaks_[carer_index]) {
//                for (const auto &out_break_item : carer_node_breaks_[carer_index]) {
//                    model.addConstr(carer_edges_[carer_index][in_break_item.first][out_break_item.first] == 0.0);
//                }
//            }
//        }

        // for each node sum of outgoing breaks is equal to sum of the incoming edges
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto num_carers = carer_edges_[carer_index].size();
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                GRBLinExpr break_outflow = 0;
                GRBLinExpr break_inflow = 0;
                for (auto node_index = 0; node_index < num_carers; ++node_index) {
                    break_outflow += carer_edges_[carer_index][node_index][break_item.first];
                    break_inflow += carer_edges_[carer_index][break_item.first][node_index];
                }
                model.addConstr(break_outflow == break_inflow);
                model.addConstr(break_outflow <= 1);
            }
        }

        // for each node outgoing flow for breaks is equal to the incoming flow
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto num_carers = carer_edges_[carer_index].size();
            for (auto node_index = begin_depot_node_; node_index <= end_depot_node_; ++node_index) {
                GRBLinExpr break_outflow = 0;
                GRBLinExpr break_inflow = 0;
                for (const auto &break_item : carer_node_breaks_[carer_index]) {
                    break_outflow += carer_edges_[carer_index][node_index][break_item.first];
                    break_inflow += carer_edges_[carer_index][break_item.first][node_index];
                }
                model.addConstr(break_outflow == break_inflow);
                model.addConstr(break_outflow <= 1);
            }
        }

        // >> at least  one break can be connected to a end depot
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            GRBLinExpr break_inflow = 0;
            for (const auto &break_item : carer_node_breaks_[carer_index]) {
                break_inflow += carer_edges_[carer_index][end_depot_node_][break_item.first];
            }
            model.addConstr(break_inflow == 1.0); //used to be >=
        }

        // one edge is incoming into a break
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto num_carer_nodes = carer_edges_[carer_index].size();
            for (const auto &break_item: carer_node_breaks_[carer_index]) {
                LOG(INFO) << carer_index << " " << break_item.first << " " << break_item.second.datetime();
                GRBLinExpr break_inflow = 0;
                for (auto node_index = 0; node_index < num_carer_nodes; ++node_index) {
                    break_inflow += carer_edges_[carer_index][node_index][break_item.first];
                }
                model.addConstr(break_inflow == 1.0);
            }
        }

        // >> set break potential for [visit] -> [break] or [depot] -> [break] connection
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &potential_item : carer_break_potentials_[carer_index]) {
                const auto break_node = potential_item.first;
                for (auto node_index = begin_depot_node_; node_index <= end_depot_node_; ++node_index) {
                    model.addConstr(potential_item.second
                                    <= (node_index + 1) * carer_edges_[carer_index][node_index][break_node]
                                       + BIG_M * (1 - carer_edges_[carer_index][node_index][break_node]));
                }
            }
        }

        // set break potential for [break] -> [visit] or [break] -> [depot] connection
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &potential_item : carer_break_potentials_[carer_index]) {
                const auto break_node = potential_item.first;
                for (auto node_index = begin_depot_node_; node_index <= end_depot_node_; ++node_index) {
                    model.addConstr((node_index + 1) * carer_edges_[carer_index][break_node][node_index]
                                    <= potential_item.second
                                       + BIG_M * (1 - carer_edges_[carer_index][break_node][node_index]));
                }
            }
        }

        // break potential propagation for [break] -> [break] connections
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &in_potential_item : carer_break_potentials_[carer_index]) {
                for (const auto &out_potential_item : carer_break_potentials_[carer_index]) {
                    if (in_potential_item.first == out_potential_item.first) { continue; }

                    model.addConstr(out_potential_item.second <= in_potential_item.second + BIG_M * (1.0 -
                                                                                                     carer_edges_[carer_index][in_potential_item.first][out_potential_item.first]));
                }
            }
        }

        // start time is propagated across connected breaks
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            for (const auto &input_break_item: carer_break_start_times_[carer_index]) {
                for (const auto &output_break_item: carer_break_start_times_[carer_index]) {
                    if (input_break_item.first == output_break_item.first) { continue; }
                    model.addConstr(input_break_item.second +
                                    carer_node_breaks_[carer_index].at(
                                            input_break_item.first).duration().total_seconds()
                                    <= output_break_item.second + BIG_M * (1 -
                                                                           carer_edges_[carer_index][input_break_item.first][output_break_item.first]));
                }
            }
        }

        // 12 - active nodes
        for (auto visit_node = first_visit_node_; visit_node <= last_visit_node_; ++visit_node) {
            GRBLinExpr visit_inflow = 0;
            for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
                for (auto from_node = begin_depot_node_; from_node <= last_visit_node_; ++from_node) {
                    visit_inflow += carer_edges_[carer_index][from_node][visit_node];
                }

            }

            model.addConstr(visit_inflow == active_visits_[visit_node]);
        }

        // 13 - both nodes of a multiple carer visit are active
        for (const auto &visit_nodes : multiple_carer_visit_nodes_) {
            model.addConstr(active_visits_[visit_nodes.first] == active_visits_[visit_nodes.second]);
        }

        // 14 - both nodes of a multiple carer visit start at the same time
        for (const auto &visit_nodes : multiple_carer_visit_nodes_) {
            model.addConstr(visit_start_times_[visit_nodes.first] == visit_start_times_[visit_nodes.second]);
        }

        // 15 - box constraints - start times for visits
        for (const auto &visit_nodes : visit_start_times_) {
            GRBLinExpr left = active_visits_[visit_nodes.first] *
                              (node_visits_[visit_nodes.first].datetime().time_of_day() -
                               visit_time_window_).total_seconds();

            GRBLinExpr right = active_visits_[visit_nodes.first] *
                               (node_visits_[visit_nodes.first].datetime().time_of_day() +
                                visit_time_window_).total_seconds();

            model.addConstr(left <= visit_start_times_[visit_nodes.first]);
            model.addConstr(visit_start_times_[visit_nodes.first] <= right);
        }

        // 16 - box constraints - start times for breaks
        for (auto carer_index = 0; carer_index < num_carers_; ++carer_index) {
            const auto carer_num_nodes = carer_edges_[carer_index].size();

            // first and last break are treated differently
            const auto first_break_node = end_depot_node_ + 1;
            const auto last_break_node = carer_num_nodes - 1;

            if (first_break_node < carer_num_nodes) {
                model.addConstr(carer_break_start_times_[carer_index][first_break_node]
                                ==
                                carer_node_breaks_[carer_index].at(
                                        first_break_node).datetime().time_of_day().total_seconds());
            }

            if (last_break_node > end_depot_node_) {
                model.addConstr(carer_break_start_times_[carer_index][last_break_node]
                                ==
                                carer_node_breaks_[carer_index].at(
                                        last_break_node).datetime().time_of_day().total_seconds());
            }

            for (auto break_node = first_break_node + 1; break_node < last_break_node; ++break_node) {
                model.addConstr((carer_node_breaks_[carer_index].at(break_node).datetime().time_of_day()
                                 - break_time_window_).total_seconds()
                                <= carer_break_start_times_[carer_index][break_node]);
                model.addConstr(carer_break_start_times_[carer_index][break_node]
                                <= (carer_node_breaks_[carer_index].at(break_node).datetime().time_of_day()
                                    + break_time_window_).total_seconds());
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

        if (solution_opt) { SetInitialSolution(solution_opt.get()); }
    }

    void SetInitialSolution(const rows::Solution &solution) {
        std::unordered_set<std::size_t> visited_nodes;

        for (const auto &carer : solution.Carers()) {
            const auto route = solution.GetRoute(carer);
            const auto carer_index = GetIndex(carer);

            std::vector<std::pair<std::size_t, std::size_t>> solution_edges;
            auto last_node = begin_depot_node_;
            for (const auto &visit : route.visits()) {
                const auto node_ids = GetNodes(visit.calendar_visit().get());
                std::size_t current_node = 0;
                auto node_found = false;
                for (auto node_id : node_ids) {
                    if (visited_nodes.find(node_id) == std::end(visited_nodes)) {
                        current_node = node_id;
                        node_found = true;
                        visited_nodes.insert(node_id);
                        break;
                    }
                }

                CHECK(node_found);

                solution_edges.emplace_back(last_node, current_node);
                last_node = current_node;
            }
            solution_edges.emplace_back(last_node, end_depot_node_);

            for (auto from_node = begin_depot_node_; from_node <= end_depot_node_; ++from_node) {
                for (auto to_node = begin_depot_node_; to_node <= end_depot_node_; ++to_node) {
                    carer_edges_[carer_index][from_node][to_node].set(GRB_DoubleAttr_Start, 0.0);
                }
            }

            for (const auto &edge : solution_edges) {
                carer_edges_[carer_index][edge.first][edge.second].set(GRB_DoubleAttr_Start, 1.0);
            }
        }

        for (const auto &visit : solution.visits()) {
            if (visit.carer()) {
                for (const auto visit_node : GetNodes(visit.calendar_visit().get())) {
                    // DO NOT SET START TIME of visits - impossible to infer in the solution combined with breaks
                    active_visits_[visit_node].set(GRB_DoubleAttr_Start, 1.0);
                }
            } else {
                for (const auto visit_node: GetNodes(visit.calendar_visit().get())) {
                    visit_start_times_[visit_node].set(GRB_DoubleAttr_Start, 0.0);
                    active_visits_[visit_node].set(GRB_DoubleAttr_Start, 0.0);
                }
            }
        }

        // DO NOT SET BREAKS
    }

    std::vector<std::size_t> GetNodes(const rows::CalendarVisit &visit) {
        std::vector<std::size_t> result;
        for (const auto &node_visit : node_visits_) {
            if (node_visit.second.id() == visit.id()) {
                result.push_back(node_visit.first);
            }
        }

        if (result.empty()) {
            LOG(FATAL) << "Visit " << visit.id() << " is not present in the problem definition";
        }

        return result;
    }

    std::size_t GetNode(const std::size_t carer_index, const rows::Break &break_element) {
        for (const auto &break_item : carer_node_breaks_[carer_index]) {
            if (break_item.second == break_element) {
                return break_item.first;
            }
        }

        return -1;
    }

    std::size_t GetNodeOrNeighbor(const std::size_t carer_index, const rows::Break &break_element) {
        boost::posix_time::ptime min_start = boost::posix_time::max_date_time;
        boost::posix_time::ptime max_start = boost::posix_time::min_date_time;

        boost::optional<std::size_t> candidate_node = boost::none;
        for (const auto &break_item : carer_node_breaks_[carer_index]) {
            min_start = std::min(min_start, break_item.second.datetime());
            max_start = std::max(max_start, break_item.second.datetime());

            if (break_item.second.carer() != break_element.carer()) {
                continue;
            }

            auto start_time_diff = break_item.second.datetime() - break_element.datetime();
            if (start_time_diff.is_negative()) {
                start_time_diff = -start_time_diff;
            }

            auto duration_diff = break_item.second.duration() - break_element.duration();
            if (duration_diff.is_negative()) {
                duration_diff = -duration_diff;
            }

            if (start_time_diff <= break_time_window_ && duration_diff <= overtime_window_) {
                candidate_node = break_item.first;
            }
        }

        if (!candidate_node) {
            LOG(FATAL) << break_element << " is not present in the problem definition";
        }

        const auto &break_ref = carer_node_breaks_[carer_index].at(candidate_node.get());
        if (break_ref.datetime() == min_start
            && break_ref.datetime() == break_element.datetime()
            && break_ref.duration() + overtime_window_ >= break_element.duration()) { // first break
            return candidate_node.get();
        } else if (break_ref.datetime() == max_start
                   && break_ref.datetime() + overtime_window_ >= break_element.datetime()
                   && break_ref.duration() + overtime_window_ >= break_element.duration()) { // last break
            return candidate_node.get();
        } else if (break_ref.duration() == break_element.duration()) {// is middle break
            return candidate_node.get();
        }

        LOG(FATAL) << break_element << " is not present in the problem definition";
        return -1;
    }

    std::size_t GetIndex(const rows::Carer &carer) {
        auto carer_index = 0;
        for (const auto &carer_diary : carer_diaries_) {
            if (carer_diary.first == carer) {
                return carer_index;
            }
            ++carer_index;
        }

        LOG(FATAL) << "Carer " << carer.sap_number() << " is not present in the problem definition";
    }

    std::size_t first_visit_node_;
    std::size_t last_visit_node_;
    std::size_t begin_depot_node_;
    std::size_t end_depot_node_;
    std::size_t num_carers_;

    boost::posix_time::time_duration visit_time_window_;
    boost::posix_time::time_duration break_time_window_;
    boost::posix_time::time_duration overtime_window_;

    boost::posix_time::ptime horizon_start_;
    boost::posix_time::time_duration horizon_duration_;

    std::vector<rows::CalendarVisit> visits_;
    std::vector<std::pair<rows::Carer, std::vector<rows::Diary>>> carer_diaries_;
    rows::CachedLocationContainer location_container_;

    std::unordered_map<std::size_t, rows::CalendarVisit> node_visits_;
    std::vector<std::pair<std::size_t, std::size_t> > multiple_carer_visit_nodes_;
    std::unordered_map<std::size_t, GRBVar> visit_start_times_;
    std::unordered_map<std::size_t, GRBVar> active_visits_;
    std::vector<GRBVar> begin_depot_start_;
    std::vector<GRBVar> end_depot_start_;

    std::vector<std::unordered_map<std::size_t, rows::Break>> carer_node_breaks_;
    std::vector<std::unordered_map<std::size_t, GRBVar>> carer_break_start_times_;
    std::vector<std::unordered_map<std::size_t, GRBVar>> carer_break_potentials_;

    std::vector<std::vector<std::vector<GRBVar>>> carer_edges_;
};

int main(int argc, char *argv[]) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    const auto printer = util::CreatePrinter(util::TEXT_FORMAT);
    const auto problem = util::LoadProblem(FLAGS_problem, printer);
    auto engine_config = util::CreateEngineConfig(FLAGS_maps);
    std::shared_ptr<std::atomic_bool> cancel_token{std::make_shared<std::atomic<bool> >(false)};

    boost::posix_time::time_duration mip_time_limit
            = util::GetTimeDurationOrDefault(FLAGS_time_limit, DEFAULT_TIME_LIMIT);
    boost::posix_time::time_duration visit_time_window{boost::posix_time::minutes(90)};
    boost::posix_time::time_duration break_time_window{boost::posix_time::minutes(90)};
    boost::posix_time::time_duration overtime_window{boost::posix_time::minutes(15)};
    boost::posix_time::time_duration no_progress_time_limit{boost::posix_time::minutes(1)};

    boost::optional<rows::Solution> solution_opt = boost::none;
    if (!FLAGS_solution.empty()) {
        solution_opt = util::LoadSolution(FLAGS_solution, problem, visit_time_window);
        LOG(INFO) << "Loaded an initial guess from the file: " << FLAGS_solution;
    }

    static const auto USE_TABU_SEARCH = false;
    const auto search_params = rows::SolverWrapper::CreateSearchParameters(USE_TABU_SEARCH);
    std::unique_ptr<rows::SecondStepSolver> solver_wrapper
            = std::make_unique<rows::SecondStepSolver>(problem,
                                                       engine_config,
                                                       search_params,
                                                       visit_time_window,
                                                       break_time_window,
                                                       overtime_window,
                                                       no_progress_time_limit);
    std::unique_ptr<operations_research::RoutingModel> routing_model
            = std::make_unique<operations_research::RoutingModel>(solver_wrapper->nodes(),
                                                                  solver_wrapper->vehicles(),
                                                                  rows::SolverWrapper::DEPOT);
    solver_wrapper->ConfigureModel(*routing_model, printer, cancel_token);
    static const rows::SolutionValidator solution_validator{};

    if (solution_opt) {
        for (const auto &visit : solution_opt.get().visits()) {
            VLOG(1) << visit;
        }

        const auto initial_routes = solver_wrapper->GetRoutes(solution_opt.get(), *routing_model);
        operations_research::Assignment *initial_assignment
                = routing_model->ReadAssignmentFromRoutes(initial_routes, false);
        if (initial_assignment == nullptr || !routing_model->solver()->CheckAssignment(initial_assignment)) {
            throw util::ApplicationError("Solution for warm start is not valid.", util::ErrorCode::ERROR);
        }
    }

    auto problem_model = Model::Create(problem, engine_config, visit_time_window, break_time_window, overtime_window);
//    problem_model.PrintStats();
    const auto ip_solution = problem_model.Solve(solution_opt, mip_time_limit);

    const auto routes = solver_wrapper->GetRoutes(ip_solution, *routing_model);
    operations_research::Assignment *assignment = routing_model->ReadAssignmentFromRoutes(routes, false);
    if (assignment == nullptr || !routing_model->solver()->CheckAssignment(assignment)) {
        throw util::ApplicationError("Solution for warm start is not valid.", util::ErrorCode::ERROR);
    }

    for (auto vehicle = 0; vehicle < solver_wrapper->vehicles(); ++vehicle) {
        const auto validation_result
                = solution_validator.ValidateFull(vehicle, *assignment, *routing_model, *solver_wrapper);
        CHECK(validation_result.error() == nullptr);
    }

    const rows::GexfWriter solution_writer;
    boost::filesystem::path output_file{FLAGS_output};
    solution_writer.Write(output_file, *solver_wrapper, *routing_model, *assignment, boost::none);

    return 0;
}