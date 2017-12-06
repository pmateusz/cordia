#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/date_time.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <nlohmann/json.hpp>

#include "util/logging.h"

#include "ortools/constraint_solver/routing.h"
#include "ortools/constraint_solver/routing_enums.pb.h"
#include "ortools/constraint_solver/routing_flags.h"

#include "location.h"
#include "location_container.h"
#include "carer.h"
#include "event.h"
#include "diary.h"
#include "visit.h"
#include "problem.h"


static bool ValidateProblemInstance(const char *flagname, const std::string &value) {
    boost::filesystem::path file_path(value);
    if (!boost::filesystem::exists(file_path)) {
        LOG(ERROR) << boost::format("File '%1%' does not exist") % file_path;
        return false;
    }

    if (!boost::filesystem::is_regular_file(file_path)) {
        LOG(ERROR) << boost::format("Path '%1%' does not point to a file") % file_path;
        return false;
    }

    return true;
}

DEFINE_string(problem_instance,
              boost::filesystem::absolute("problem.json").native(),
              "a file path to the problem instance");
DEFINE_validator(problem_instance, &ValidateProblemInstance);

int main(int argc, char **argv) {
    static const int STATUS_ERROR = 1;
    static const int STATUS_OK = 1;

    util::SetupLogging(argv[0]);

    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling");
    static const bool REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    boost::filesystem::path problem_path(boost::filesystem::canonical(FLAGS_problem_instance));
    LOG(INFO) << boost::format("Launched program with arguments: %1%") % problem_path;

    std::ifstream problem_instance_file(problem_path.c_str());
    if (!problem_instance_file.is_open()) {
        LOG(ERROR) << boost::format("Failed to open file: %1%") % problem_path;
        return STATUS_ERROR;
    }

    nlohmann::json json;
    try {
        problem_instance_file >> json;
    } catch (...) {
        LOG(ERROR) << boost::current_exception_diagnostic_information();
        return STATUS_ERROR;
    }

    rows::Problem problem;
    try {
        rows::Problem::JsonLoader json_loader;
        problem = json_loader.Load(json);
    } catch (const std::domain_error &ex) {
        LOG(ERROR) << boost::format("Failed to parse file '%1%' due to error: '%2%'") % problem_path % ex.what();
        return STATUS_ERROR;
    }

//    const operations_research::RoutingModel::NodeIndex depot(0);
//    operations_research::RoutingModel routing(visits_count + 1, carers_count, depot);
//    operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
//    parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
//    parameters.mutable_local_search_operators()->set_use_path_lns(false);
//    parameters.mutable_local_search_operators()->set_use_inactive_lns(false);

    std::cout << json;
}