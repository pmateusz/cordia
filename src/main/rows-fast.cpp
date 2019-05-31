#include <algorithm>
#include <string>
#include <functional>
#include <ostream>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <boost/date_time.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem.hpp>

#include <glog/logging.h>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/routing_flags.h>
#include <boost/exception/diagnostic_information.hpp>

#include <osrm/match_parameters.hpp>
#include <osrm/nearest_parameters.hpp>
#include <osrm/route_parameters.hpp>
#include <osrm/table_parameters.hpp>
#include <osrm/trip_parameters.hpp>
#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <osrm/osrm.hpp>
#include <boost/algorithm/string/join.hpp>

#include "util/aplication_error.h"
#include "util/error_code.h"
#include "problem.h"
#include "solution.h"


struct Visit {
    Visit(std::size_t location, const std::string &begin, const std::string &end, const std::string &duration)
            : Visit{location,
                    boost::posix_time::duration_from_string(begin),
                    boost::posix_time::duration_from_string(end),
                    boost::posix_time::duration_from_string(duration)} {}

    Visit(std::size_t location,
          boost::posix_time::time_duration begin,
          boost::posix_time::time_duration end,
          boost::posix_time::time_duration duration)
            : Location{location},
              Begin{std::move(begin)},
              End{std::move(end)},
              Duration{std::move(duration)} {}

    const std::size_t Location;
    const boost::posix_time::time_duration Begin;
    const boost::posix_time::time_duration End;
    const boost::posix_time::time_duration Duration;
};

std::ostream &operator<<(std::ostream &out, const Visit &visit) {
    out << boost::format("%1% [%2%,%3%] %4%")
           % visit.Location
           % visit.Begin
           % visit.End
           % visit.Duration;
    return out;
}

struct Break {
    Break(const std::string &start, const std::string &duration)
            : Break{boost::posix_time::duration_from_string(start),
                    boost::posix_time::duration_from_string(duration)} {}

    Break(boost::posix_time::time_duration start, boost::posix_time::time_duration duration)
            : Start{std::move(start)},
              Duration{std::move(duration)} {}

    const boost::posix_time::time_duration Start;
    const boost::posix_time::time_duration Duration;
};

struct Environment {
    static const std::string TIME_DIM;

    Environment(std::vector<Visit> visits,
                std::vector<std::vector<Break> > breaks,
                std::vector<std::vector<int64> > distances)
            : Depot{0},
              Visits(std::move(visits)),
              Breaks(std::move(breaks)),
              Distances(std::move(distances)) {}

    int64 distance(operations_research::RoutingNodeIndex from_node,
                   operations_research::RoutingNodeIndex to_node) const {
        if (from_node == Depot || to_node == Depot) {
            return 0;
        }

        const auto from = NodeToVisit(from_node).Location;
        const auto to = NodeToVisit(to_node).Location;
        return Distances.at(from).at(to);
    }

    int64 service_plus_distance(operations_research::RoutingNodeIndex from_node,
                                operations_research::RoutingNodeIndex to_node) const {
        if (from_node == Depot) {
            return 0;
        }

        const auto service_time = NodeToVisit(from_node).Duration.total_seconds();
        return service_time + distance(from_node, to_node);
    }

    const Visit &NodeToVisit(operations_research::RoutingNodeIndex node) const {
        return Visits.at(static_cast<std::size_t>(node.value() - 1));
    }

    const operations_research::RoutingNodeIndex Depot;
    const std::vector<Visit> Visits;
    const std::vector<std::vector<Break> > Breaks;
    const std::vector<std::vector<int64> > Distances;
};

const std::string Environment::TIME_DIM{"time"};

rows::Problem LoadReducedProblem(const std::string &problem_path) {
    boost::filesystem::path problem_file(boost::filesystem::canonical(problem_path));
    std::ifstream problem_stream;
    problem_stream.open(problem_file.c_str());
    if (!problem_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     util::ErrorCode::ERROR);
    }

    nlohmann::json problem_json;
    try {
        problem_stream >> problem_json;
    } catch (...) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     boost::current_exception_diagnostic_information(),
                                     util::ErrorCode::ERROR);
    }

    rows::Problem problem;
    try {
        rows::Problem::JsonLoader json_loader;
        problem = json_loader.Load(problem_json);
    } catch (const std::domain_error &ex) {
        throw util::ApplicationError(
                (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % problem_file % ex.what()).str(),
                util::ErrorCode::ERROR);
    }

    const auto timespan_pair = problem.Timespan();
    if (timespan_pair.first.date() < timespan_pair.second.date()) {
        LOG(WARNING) << boost::format("Problem '%1%' contains records from several days. " \
                                              "The computed solution will be reduced to a single day: '%2%'")
                        % problem_file
                        % timespan_pair.first.date();
    }

    const auto problem_to_use = problem.Trim(timespan_pair.first, boost::posix_time::hours(24));
    DCHECK(problem_to_use.IsAdmissible());
    return problem_to_use;
}

int main(int argc, char *argv[]) {
    static const auto TIME_WINDOW = boost::posix_time::minutes(30);
    auto problem = LoadReducedProblem("/home/pmateusz/dev/cordia/problem.json");

    std::vector<rows::CalendarVisit> visits_to_schedule;
    std::copy(std::begin(problem.visits()),
              std::begin(problem.visits()) + 50,
              std::back_inserter(visits_to_schedule));

    auto current_index = 0;
    std::unordered_map<rows::Location, std::size_t> location_index;
    std::vector<rows::Location> locations;
    for (const auto &visit : visits_to_schedule) {
        const auto &location = visit.location().get();
        if (location_index.emplace(location, current_index).second) {
            locations.push_back(location);
            ++current_index;
        }
    }

    // initialize visits
    std::vector<Visit> visits;
    for (const auto &visit : visits_to_schedule) {
        const auto location_it = location_index.find(visit.location().get());
        CHECK(location_it != std::end(location_index));

        visits.emplace_back(location_it->second,
                            visit.datetime().time_of_day() - TIME_WINDOW,
                            visit.datetime().time_of_day() + TIME_WINDOW,
                            visit.duration());
    }

    // calculate distance matrix
    std::vector<int64> prototype(locations.size(), 0);
    std::vector<std::vector<int64> > distances(locations.size(), prototype);
    prototype.clear();

    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig("/home/pmateusz/dev/cordia/data/scotland-latest.osrm");
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    const osrm::OSRM engine{config};

    const auto get_distance = [&engine](rows::Location source,
                                        rows::Location destination) -> int64 {
        osrm::RouteParameters params;
        params.coordinates.emplace_back(source.longitude(), source.latitude());
        params.coordinates.emplace_back(destination.longitude(), destination.latitude());

        osrm::json::Object result;
        engine.Route(params, result);
        auto &routes = result.values["routes"].get<osrm::json::Array>();
        auto &route = routes.values.at(0).get<osrm::json::Object>();
        return static_cast<int64>(std::ceil(route.values["duration"].get<osrm::json::Number>().value));
    };

    const auto locations_size = locations.size();
    for (auto from_index = 0; from_index < locations_size; ++from_index) {
        for (auto to_index = 0; to_index < locations_size; ++to_index) {
            distances[from_index][to_index] = get_distance(locations[from_index], locations[to_index]);
        }
    }

    static const auto FIX_CUMULATIVE_TO_ZERO = true;
    static const auto MAX_TIME_SLACK = boost::posix_time::hours(24).total_seconds();
    static const auto CAPACITY = boost::posix_time::hours(24).total_seconds();

    boost::posix_time::ptime min_date_time = boost::posix_time::min_date_time;
    for (const auto &visit : visits_to_schedule) {
        min_date_time = std::min(min_date_time, boost::posix_time::ptime{visit.datetime().date()});
    }

    boost::posix_time::time_period time_horizon(min_date_time,
                                                min_date_time + boost::posix_time::seconds(MAX_TIME_SLACK));

    // initialize breaks
    std::vector<std::vector<Break> > breaks;
    for (const auto &carer_pair : problem.carers()) {
        DCHECK(carer_pair.second.empty() || carer_pair.second.size() == 1);

        std::vector<Break> local_breaks;
        for (const auto &local_break : carer_pair.second.front().Breaks(time_horizon)) {
            local_breaks.emplace_back(local_break.begin().time_of_day(), local_break.duration());
        }
        breaks.emplace_back(std::move(local_breaks));
    }

    const Environment data{visits, breaks, distances};

    operations_research::RoutingIndexManager index_manager(static_cast<int>(data.Visits.size() + 1),
                                                           static_cast<int>(data.Breaks.size()),
                                                           data.Depot);

    operations_research::RoutingModel model(index_manager);

    const auto transit_callback_handle = model.RegisterTransitCallback(
            [&data, &index_manager](int64 from_index, int64 to_index) -> int64 {
                return data.distance(index_manager.IndexToNode(from_index), index_manager.IndexToNode(to_index));
            });

    model.SetArcCostEvaluatorOfAllVehicles(transit_callback_handle);

    const auto service_time_callback_handle = model.RegisterTransitCallback(
            [&data, &index_manager](int64 from_index, int64 to_index) -> int64 {
                return data.service_plus_distance(index_manager.IndexToNode(from_index),
                                                  index_manager.IndexToNode(to_index));
            });
    model.AddDimension(service_time_callback_handle,
                       MAX_TIME_SLACK,
                       CAPACITY,
                       FIX_CUMULATIVE_TO_ZERO,
                       Environment::TIME_DIM);

    auto time_dimension = model.GetMutableDimension(Environment::TIME_DIM);

    for (auto visit_node = data.Depot + 1; visit_node < model.nodes(); ++visit_node) {
        const auto &visit = data.NodeToVisit(visit_node);
        const auto visit_index = index_manager.NodeToIndex(visit_node);

        time_dimension->CumulVar(visit_index)->SetRange(visit.Begin.total_seconds(), visit.End.total_seconds());
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(visit_index));
        model.AddToAssignment(time_dimension->SlackVar(visit_index));

        static const auto DROP_PENALTY = 1000000;
        model.AddDisjunction({visit_index}, DROP_PENALTY);
    }

    for (auto variable_index = 0; variable_index < model.Size(); ++variable_index) {
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(variable_index));
    }

    for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        std::vector<operations_research::IntervalVar *> break_intervals;
        auto break_index = 0;
        for (const auto &break_config : data.Breaks[vehicle]) {
            auto interval = model.solver()->MakeFixedInterval(break_config.Start.total_seconds(),
                                                              break_config.Duration.total_seconds(),
                                                              (boost::format("Break %1% of vehicle %2%")
                                                               % break_index
                                                               % vehicle).str());
            break_intervals.emplace_back(interval);
        }

        time_dimension->SetBreakIntervalsOfVehicle(std::move(break_intervals), vehicle, {});
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(model.Start(vehicle)));
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(model.End(vehicle)));
    }

    operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
    parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
//
//    static const auto USE_ADVANCED_SEARCH = false;
//    parameters.set_use_light_propagation(USE_ADVANCED_SEARCH);
//    parameters.mutable_local_search_operators()->set_use_path_lns(USE_ADVANCED_SEARCH);
//    parameters.mutable_local_search_operators()->set_use_inactive_lns(USE_ADVANCED_SEARCH);

    model.CloseModelWithParameters(parameters);

    const operations_research::Assignment *assignment = model.SolveWithParameters(parameters);

    if (nullptr == assignment) {
        LOG(ERROR) << "No solution found";
        return 1;
    }

    LOG(INFO) << model.solver()->DebugString();

    operations_research::Assignment assignment_check_copy{assignment};
    if (!model.solver()->CheckAssignment(&assignment_check_copy)) {
        LOG(ERROR) << "Solution failed validation checks";
        return 1;
    }

    std::vector<std::tuple<rows::Carer, rows::Diary, Visit, boost::posix_time::time_period, boost::posix_time::time_period> > schedule;
    const auto date = problem.visits().front().datetime().date();
    for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        auto current_visit_index = model.Start(vehicle);
        const auto &carer_pair = problem.carers().at(static_cast<std::size_t>(vehicle));
        if (!model.IsEnd(assignment->Value(model.NextVar(current_visit_index)))) {
            current_visit_index = assignment->Value(model.NextVar(current_visit_index));
            while (!model.IsEnd(current_visit_index)) {
                const auto visit_node = index_manager.IndexToNode(current_visit_index);
                CHECK_NE(visit_node, data.Depot);

                const auto &visit = data.NodeToVisit(visit_node);
                const boost::posix_time::time_period min_period(
                        boost::posix_time::ptime(date, boost::posix_time::seconds(
                                assignment->Min(time_dimension->CumulVar(current_visit_index)))),
                        visit.Duration);
                const boost::posix_time::time_period max_period(
                        boost::posix_time::ptime(date, boost::posix_time::seconds(
                                assignment->Max(time_dimension->CumulVar(current_visit_index)))),
                        visit.Duration);

                schedule.emplace_back(carer_pair.first, carer_pair.second.front(), visit, min_period, max_period);

                current_visit_index = assignment->Value(model.NextVar(current_visit_index));
            }
        }
    }

    for (const auto &record : schedule) {
        const auto &diary = std::get<1>(record);
        const auto &min_period = std::get<3>(record);
        const auto &max_period = std::get<4>(record);

        for (const auto &event : diary.Breaks(time_horizon)) {
            const auto &event_period = event.period();
            const auto &min_intersection = event_period.intersection(min_period);
            const auto &max_intersection = event_period.intersection(max_period);

            LOG_IF(ERROR, !min_intersection.is_null()) << "Min intersection overlaps with break: " << event_period
                                                       << " intersection: " << min_intersection
                                                       << " carer: " << std::get<0>(record)
                                                       << " visit: " << std::get<2>(record);
            LOG_IF(ERROR, !max_intersection.is_null()) << "Max intersection overlaps with break: " << event_period
                                                       << " intersection: " << min_intersection
                                                       << " carer: " << std::get<0>(record)
                                                       << " visit: " << std::get<2>(record);
        }
    }


    for (const auto visit : data.Visits) {
        LOG(INFO) << boost::format("Visit %1% [%2%,%3%] %4%")
                     % visit.Location
                     % visit.Begin
                     % visit.End
                     % visit.Duration;
    }

    std::unordered_map<rows::Carer, std::size_t> carer_index;
    current_index = 0;
    for (const auto &carer_pair : problem.carers()) {
        CHECK(carer_index.emplace(carer_pair.first, current_index++).second);
    }

    std::unordered_set<rows::Carer> used_carers;
    for (const auto &record: schedule) {
        used_carers.insert(std::get<0>(record));
    }

    for (const auto &local_carer : used_carers) {
        const auto local_carer_index = carer_index[local_carer];
        LOG(INFO) << "Carer: " << local_carer_index << " breaks:";
        for (const auto &local_break : data.Breaks.at(local_carer_index)) {
            LOG(INFO) << boost::format("[%1%, %2%]")
                         % local_break.Start
                         % local_break.Duration;
        }
    }

    LOG(INFO) << "Distance matrix";
    for (const auto &row : data.Distances) {
        std::vector<std::string> text_row;
        std::transform(std::begin(row), std::end(row), std::back_inserter(text_row),
                       [](const int64 distance) -> std::string { return std::to_string(distance); });
        LOG(INFO) << boost::algorithm::join(text_row, ", ");
    }
}