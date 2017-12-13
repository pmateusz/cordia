#include "solver_wrapper.h"

#include <algorithm>
#include <numeric>

#include <boost/date_time/posix_time/ptime.hpp>

#include "location.h"

namespace rows {

    const operations_research::RoutingModel::NodeIndex SolverWrapper::DEPOT{0};

    const int64 SolverWrapper::SECONDS_IN_DAY = 24 * 3600;

    const std::string SolverWrapper::TIME_DIMENSION{"Time"};

    SolverWrapper::SolverWrapper(const rows::Problem &problem, osrm::EngineConfig &config)
            : SolverWrapper(problem, GetUniqueLocations(problem), config) {}

    SolverWrapper::SolverWrapper(const rows::Problem &problem, const std::vector<rows::Location> &locations,
                                 osrm::EngineConfig &config)
            : problem_(problem),
              location_container_(std::cbegin(locations), std::cend(locations), config) {}

    int64 SolverWrapper::Distance(operations_research::RoutingModel::NodeIndex from,
                                  operations_research::RoutingModel::NodeIndex to) {
        // FIXME:
        return 0;

        if (from == DEPOT || to == DEPOT) {
            return 0;
        }

        return location_container_.Distance(Visit(from).location(), Visit(to).location());
    }

    int64 SolverWrapper::ServiceTimePlusDistance(operations_research::RoutingModel::NodeIndex from,
                                                 operations_research::RoutingModel::NodeIndex to) {
        if (from == DEPOT) {
            return 0;
        }

        const auto visit = Visit(from);
        const auto value = visit.duration().total_seconds() + Distance(from, to);
        return value;
    }

    Location SolverWrapper::GetCentralLocation() const {
        double latitude = 0, longitude = 0;

        for (const auto &visit : problem_.visits()) {
            const auto &location = visit.location();
            latitude += static_cast<double>(location.latitude());
            longitude += static_cast<double>(location.longitude());
        }

        const auto denominator = std::max(1.0, static_cast<double>(problem_.visits().size()));
        return Location(osrm::util::FloatLatitude{latitude / denominator},
                        osrm::util::FloatLongitude{longitude / denominator});
    }

    rows::Visit SolverWrapper::Visit(const operations_research::RoutingModel::NodeIndex visit) const {
        DCHECK_NE(visit, DEPOT);

        return problem_.visits()[visit.value() - 1];
    }

    rows::Diary SolverWrapper::Diary(const operations_research::RoutingModel::NodeIndex carer) const {
        const auto carer_pair = problem_.carers().at(static_cast<std::size_t>(carer.value()));
        DCHECK_EQ(carer_pair.second.size(), 1);
        return {carer_pair.second.front()};
    }

    rows::Carer SolverWrapper::Carer(const operations_research::RoutingModel::NodeIndex carer) const {
        const auto &carer_pair = problem_.carers()[carer.value()];
        return {carer_pair.first};
    }

    std::vector<operations_research::IntervalVar *> SolverWrapper::Breaks(operations_research::Solver *const solver,
                                                                          const operations_research::RoutingModel::NodeIndex carer) const {
        std::vector<operations_research::IntervalVar *> result;

        const auto &diary = Diary(carer);

        boost::posix_time::ptime last_end_time(diary.date());
        boost::posix_time::ptime next_day(diary.date() + boost::gregorian::date_duration(1));

        BreakType break_type = BreakType::BEFORE_WORKDAY;
        for (const auto &event : diary.events()) {

            result.push_back(CreateBreak(solver,
                                         last_end_time.time_of_day(),
                                         boost::posix_time::time_period(last_end_time, event.begin()).length(),
                                         GetBreakLabel(carer, break_type)));

            last_end_time = event.end();
            break_type = BreakType::BREAK;
        }

        break_type = BreakType::AFTER_WORKDAY;
        result.push_back(CreateBreak(solver,
                                     last_end_time.time_of_day(),
                                     boost::posix_time::time_period(last_end_time, next_day).length(),
                                     GetBreakLabel(carer, break_type)));

        return result;
    }

    std::string SolverWrapper::GetBreakLabel(const operations_research::RoutingModel::NodeIndex carer,
                                             SolverWrapper::BreakType break_type) {
        switch (break_type) {
            case BreakType::BEFORE_WORKDAY:
                return (boost::format("Carer '%1%' before workday") % carer).str();
            case BreakType::AFTER_WORKDAY:
                return (boost::format("Carer '%1%' after workday") % carer).str();
            case BreakType::BREAK:
                return (boost::format("Carer '%1%' break") % carer).str();
            default:
                throw std::domain_error((boost::format("Handling label '%1%' is not implemented") % carer).str());
        }
    }

    std::vector<operations_research::RoutingModel::NodeIndex> SolverWrapper::Carers() const {
        std::vector<operations_research::RoutingModel::NodeIndex> result(problem_.carers().size());
        std::iota(std::begin(result), std::end(result), operations_research::RoutingModel::NodeIndex(0));
        return result;
    }

    int SolverWrapper::NodesCount() const {
        return static_cast<int>(problem_.visits().size() + 1);
    }

    int SolverWrapper::VehicleCount() const {
        return static_cast<int>(problem_.carers().size());
    }

    void SolverWrapper::DisplayPlan(const operations_research::RoutingModel &routing,
                                    const operations_research::Assignment &plan, bool use_same_vehicle_costs,
                                    int64 max_nodes_per_group, int64 same_vehicle_cost,
                                    const operations_research::RoutingDimension &time_dimension) {
        std::stringstream out;
        out << boost::format("Cost %1% ") % plan.ObjectiveValue() << std::endl;

        std::stringstream dropped_stream;
        for (int order = 1; order < routing.nodes(); ++order) {
            if (plan.Value(routing.NextVar(order)) == order) {
                if (dropped_stream.rdbuf()->in_avail() == 0) {
                    dropped_stream << ' ' << order;
                } else {
                    dropped_stream << ',' << ' ' << order;
                }
            }
        }

        if (dropped_stream.rdbuf()->in_avail() > 0) {
            out << "Dropped orders:" << dropped_stream.str() << std::endl;
        }

        if (use_same_vehicle_costs) {
            int group_size = 0;
            int64 group_same_vehicle_cost = 0;
            std::set<int64> visited;
            const operations_research::RoutingModel::NodeIndex kFirstNodeAfterDepot(1);
            for (operations_research::RoutingModel::NodeIndex order = kFirstNodeAfterDepot;
                 order < routing.nodes(); ++order) {
                ++group_size;
                visited.insert(plan.Value(routing.VehicleVar(routing.NodeToIndex(order))));
                if (group_size == max_nodes_per_group) {
                    if (visited.size() > 1) {
                        group_same_vehicle_cost += (visited.size() - 1) * same_vehicle_cost;
                    }
                    group_size = 0;
                    visited.clear();
                }
            }
            if (visited.size() > 1) {
                group_same_vehicle_cost += (visited.size() - 1) * same_vehicle_cost;
            }
            LOG(INFO) << "Same vehicle costs: " << group_same_vehicle_cost;
        }

        for (int route_number = 0; route_number < routing.vehicles(); ++route_number) {
            int64 order = routing.Start(route_number);
            out << boost::format("Route %1%: ") % route_number;

            if (routing.IsEnd(plan.Value(routing.NextVar(order)))) {
                out << "Empty" << std::endl;
            } else {
                while (true) {
                    operations_research::IntVar *const time_var =
                            time_dimension.CumulVar(order);
                    operations_research::IntVar *const slack_var =
                            routing.IsEnd(order) ? nullptr : time_dimension.SlackVar(order);
                    if (slack_var != nullptr && plan.Contains(slack_var)) {
                        out << boost::format("%1% Time(%2%, %3%) Slack(%4%, %5%) -> ")
                               % order
                               % plan.Min(time_var) % plan.Max(time_var)
                               % plan.Min(slack_var) % plan.Max(slack_var);
                    } else {
                        out << boost::format("%1% Time(%2%, %3%) ->")
                               % order
                               % plan.Min(time_var)
                               % plan.Max(time_var);
                    }
                    if (routing.IsEnd(order)) break;
                    order = plan.Value(routing.NextVar(order));
                }
                out << std::endl;
            }
        }
        LOG(INFO) << out.str();
    }

    operations_research::IntervalVar *SolverWrapper::CreateBreak(operations_research::Solver *const solver,
                                                                 const boost::posix_time::time_duration &start_time,
                                                                 const boost::posix_time::time_duration &duration,
                                                                 const std::string &label) {
        static const auto IS_OPTIONAL = false;

        return solver->MakeFixedDurationIntervalVar(
                /*start min*/ start_time.total_seconds(),
                /*start max*/ start_time.total_seconds(),
                              duration.total_seconds(),
                              IS_OPTIONAL,
                              label);
    }

    void SolverWrapper::ComputeDistances() {
        location_container_.ComputeDistances();
    }

    std::vector<rows::Location> SolverWrapper::GetUniqueLocations(const rows::Problem &problem) {
        std::unordered_set<rows::Location> location_set;
        for (const auto &visit : problem.visits()) {
            location_set.insert(visit.location());
        }
        return std::vector<rows::Location>(std::cbegin(location_set), std::cend(location_set));
    }
}
