#include <vector>
#include <string>

#include "boost/filesystem.hpp"
#include "boost/format.hpp"
#include "boost/date_time.hpp"
#include "boost/date_time/gregorian/gregorian.hpp"
#include "boost/date_time/posix_time/posix_time.hpp"

#include "gflags/gflags.h"
#include "glog/logging.h"

#include "nlohmann/json.hpp"

#include "util/logging.h"

#include "ortools/constraint_solver/routing.h"
#include "ortools/constraint_solver/routing_enums.pb.h"
#include "ortools/constraint_solver/routing_flags.h"

#include "location.h"
#include "location_container.h"
#include "visit.h"

DEFINE_string(problem_instance,
              boost::filesystem::absolute("problem.json").native(),
              "a file path to the problem instance");

const int STATUS_ERROR = 1;
const int STATUS_OK = 1;

template<typename JsonType>
std::vector<rows::Visit> LoadVisits(const std::string &problem_path, const JsonType &json) {
    std::vector<rows::Visit> result;

    const auto place_visits_it = json.find("visits");
    if (place_visits_it == std::end(json)) {
        LOG(ERROR) << boost::format("Document %1% is not correctly formatted. '%2%' key not found.")
                      % problem_path
                      % "visits";
        return result;
    }

    const auto carers_it = json.find("carers");
    if (carers_it == std::end(json)) {
        LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found.")
                      % problem_path
                      % "carers";
        return result;
    }

    for (const auto &place_visits : place_visits_it.value()) {
        LOG(INFO) << place_visits;

        const auto location_json_it = place_visits.find("location");
        if (location_json_it == std::end(place_visits)) {
            LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found.")
                          % problem_path
                          % "location";
            return result;
        }

        const auto &location_json = location_json_it.value();
        const auto latitude_it = location_json.find("latitude");
        if (latitude_it == std::end(location_json)) {
            LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found")
                          % problem_path
                          % "latitude";
            return result;
        }
        const auto latitude = latitude_it.value().template get<std::string>();

        const auto longitude_it = location_json.find("longitude");
        if (longitude_it == std::end(location_json)) {
            LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found")
                          % problem_path
                          % "longitude";
            return result;
        }
        const auto longitude = longitude_it.value().template get<std::string>();

        rows::Location location(latitude, longitude);

        const auto visits_json_it = place_visits.find("visits");
        if (visits_json_it == std::end(place_visits)) {
            LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found")
                          % problem_path
                          % "visits";
            return result;
        }

        for (const auto &visit_json : visits_json_it.value()) {
            const auto date_it = visit_json.find("date");
            if (date_it == std::end(visit_json)) {
                LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found")
                              % problem_path
                              % "date";
                return result;
            }
            auto date = boost::gregorian::from_simple_string(date_it.value().template get<std::string>());

            const auto time_it = visit_json.find("time");
            if (time_it == std::end(visit_json)) {
                LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found")
                              % problem_path
                              % "time";
                return result;
            }
            const auto time = boost::posix_time::duration_from_string(time_it.value().template get<std::string>());

            const auto duration_it = visit_json.find("duration");
            if (duration_it == std::end(visit_json)) {
                LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found")
                              % problem_path
                              % "duration";
                return result;
            }
            boost::posix_time::time_duration duration = boost::posix_time::seconds(
                    std::stol(duration_it.value().template get<std::string>()));

            result.emplace_back(location, date, time, duration);
        }
    }

    return result;
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);

    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling");
    static const bool REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    boost::filesystem::path raw_problem_path(FLAGS_problem_instance);
    if (!boost::filesystem::exists(raw_problem_path)) {
        LOG(ERROR) << boost::format("File '%1%' does not exist") % raw_problem_path;
        return STATUS_ERROR;
    }

    if (!boost::filesystem::is_regular_file(raw_problem_path)) {
        LOG(ERROR) << boost::format("Path '%1%' does not point to a file") % raw_problem_path;
        return STATUS_ERROR;
    }
    boost::filesystem::path problem_path(boost::filesystem::canonical(raw_problem_path));

    LOG(INFO) << boost::format("Launched program with arguments: %1%") % problem_path;

    std::ifstream problem_instance_file(problem_path.c_str());
    if (!problem_instance_file.is_open()) {
        LOG(ERROR) << boost::format("Failed to open file: %1%") % problem_path;
        return STATUS_ERROR;
    }

    nlohmann::json json;
    problem_instance_file >> json;

    const auto carers_it = json.find("carers");
    if (carers_it == std::end(json)) {
        LOG(ERROR) << boost::format("Document %1% is not formatted correctly. '%2%' key not found.")
                      % problem_path
                      % "carers";
        return STATUS_ERROR;
    }

    const auto visits = LoadVisits(problem_path.string(), json);

//    const operations_research::RoutingModel::NodeIndex depot(0);
//    operations_research::RoutingModel routing(visits_count + 1, carers_count, depot);
//    operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
//    parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
//    parameters.mutable_local_search_operators()->set_use_path_lns(false);
//    parameters.mutable_local_search_operators()->set_use_inactive_lns(false);



    std::cout << json;
}