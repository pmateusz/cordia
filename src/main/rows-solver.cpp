#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/range/irange.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <nlohmann/json.hpp>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/routing_flags.h>

#include <osrm/engine/engine_config.hpp>
#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <osrm/osrm.hpp>

#include "location.h"
#include "location_container.h"
#include "carer.h"
#include "event.h"
#include "diary.h"
#include "visit.h"
#include "problem.h"
#include "solver_wrapper.h"
#include "util/logging.h"


static bool ValidateFilePath(const char *flagname, const std::string &value) {
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

DEFINE_string(problem_file, "problem.json", "a file path to the problem instance");
DEFINE_validator(problem_file, &ValidateFilePath);

DEFINE_string(map_file,
              boost::filesystem::canonical("../data/scotland-latest.osrm").string(),
              "a file path to the map");
DEFINE_validator(map_file, &ValidateFilePath);

rows::Problem Reduce(const rows::Problem &problem, const boost::filesystem::path &problem_file) {
    std::set<boost::gregorian::date> days;
    for (const auto &visit : problem.visits()) {
        days.insert(visit.date());
    }
    boost::gregorian::date day_to_use = *std::min_element(std::begin(days), std::end(days));

    if (days.size() > 1) {
        LOG(WARNING) << boost::format("Problem '%1%' contains records from several days. " \
                                              "The computed solution will be reduced to a single day: '%2%'")
                        % problem_file
                        % day_to_use;
    }

    std::vector<rows::Visit> visits_to_use;
    for (const auto &visit : problem.visits()) {
        if (visit.date() == day_to_use) {
            visits_to_use.push_back(visit);
        }
    }

    std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > carers_to_use;
    for (const auto &carer_diaries : problem.carers()) {
        for (const auto &diary : carer_diaries.second) {
            if (diary.date() == day_to_use) {
                carers_to_use.emplace_back(carer_diaries.first, std::vector<rows::Diary>{diary});
            }
        }
    }

// TODO:
//    std::vector<rows::Visit> reduced_visits;
//    std::copy(std::begin(visits_to_use), std::begin(visits_to_use) + 150, std::back_inserter(reduced_visits));

    return {visits_to_use, carers_to_use};
}

int main(int argc, char **argv) {
    static const int STATUS_ERROR = 1;
    static const int STATUS_OK = 1;

    util::SetupLogging(argv[0]);

    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling");
    static const bool REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    boost::filesystem::path problem_file(boost::filesystem::canonical(FLAGS_problem_file));
    LOG(INFO) << boost::format("Launched program with arguments: %1%") % problem_file;

    std::ifstream problem_stream(problem_file.c_str());
    if (!problem_stream.is_open()) {
        LOG(ERROR) << boost::format("Failed to open file: %1%") % problem_file;
        return STATUS_ERROR;
    }

    nlohmann::json json;
    try {
        problem_stream >> json;
    } catch (...) {
        LOG(ERROR) << boost::current_exception_diagnostic_information();
        return STATUS_ERROR;
    }

    rows::Problem problem;
    try {
        rows::Problem::JsonLoader json_loader;
        problem = json_loader.Load(json);
    } catch (const std::domain_error &ex) {
        LOG(ERROR) << boost::format("Failed to parse file '%1%' due to error: '%2%'") % problem_file % ex.what();
        return STATUS_ERROR;
    }

    rows::Problem reduced_problem = Reduce(problem, problem_file);
    DCHECK(reduced_problem.IsAdmissible());

    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig(FLAGS_map_file);
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;
    DCHECK(config.IsValid());

    rows::SolverWrapper wrapper(reduced_problem, config);
    wrapper.ComputeDistances();

    operations_research::RoutingModel routing(wrapper.NodesCount(), wrapper.VehicleCount(), rows::SolverWrapper::DEPOT);

    routing.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(&wrapper, &rows::SolverWrapper::Distance));

    routing.AddDimension(NewPermanentCallback(&wrapper, &rows::SolverWrapper::ServiceTimePlusDistance),
                         rows::SolverWrapper::SECONDS_IN_DAY,
                         rows::SolverWrapper::SECONDS_IN_DAY,
            /*fix_start_cumul_to_zero=*/ false,
                         rows::SolverWrapper::TIME_DIMENSION);

    operations_research::RoutingDimension *const time_dimension = routing.GetMutableDimension(
            rows::SolverWrapper::TIME_DIMENSION);

    operations_research::Solver *const solver = routing.solver();
    for (const auto &carer_index : wrapper.Carers()) {
        time_dimension->SetBreakIntervalsOfVehicle(wrapper.Breaks(solver, carer_index), carer_index.value());
    }

    // set time windows
    for (auto order = 1; order < routing.nodes(); ++order) {
        const auto &visit = wrapper.Visit(operations_research::RoutingModel::NodeIndex(order));
        time_dimension->CumulVar(order)->SetRange(visit.time().total_seconds(),
                                                  (visit.time() + visit.duration()).total_seconds());
        routing.AddToAssignment(time_dimension->SlackVar(order));
    }

    // minimize time variables
    for (auto variable_index = 0; variable_index < routing.Size(); ++variable_index) {
        routing.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(variable_index));
    }

    // minimize route duration
    for (auto carer_index = 0; carer_index < routing.vehicles(); ++carer_index) {
        routing.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(routing.Start(carer_index)));
        routing.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(routing.End(carer_index)));
    }

    // Adding penalty costs to allow skipping orders.
    const int64 kPenalty = 100000;
    const operations_research::RoutingModel::NodeIndex kFirstNodeAfterDepot(1);
    for (operations_research::RoutingModel::NodeIndex order = kFirstNodeAfterDepot; order < routing.nodes(); ++order) {
        std::vector<operations_research::RoutingModel::NodeIndex> orders(1, order);
        routing.AddDisjunction(orders, kPenalty);
    }

    operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
    parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
    parameters.mutable_local_search_operators()->set_use_path_lns(false);
    parameters.mutable_local_search_operators()->set_use_inactive_lns(false);

    const operations_research::Assignment *solution = routing.SolveWithParameters(parameters);
    if (solution == nullptr) {
        LOG(INFO) << "No solution found.";
        return STATUS_ERROR;
    }
    wrapper.DisplayPlan(routing,
                        *solution,
            /*use_same_vehicle_costs=*/false,
            /*max_nodes_per_group=*/0,
            /*same_vehicle_cost=*/0,
                        routing.GetDimensionOrDie(rows::SolverWrapper::TIME_DIMENSION));

    return STATUS_OK;
}