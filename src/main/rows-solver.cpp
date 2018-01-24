#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <memory>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/range/irange.hpp>
#include <boost/algorithm/string/join.hpp>

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

#include <libgexf/libgexf.h>

#include "util/aplication_error.h"
#include "util/logging.h"
#include "util/validation.h"
#include "location.h"
#include "location_container.h"
#include "carer.h"
#include "event.h"
#include "diary.h"
#include "calendar_visit.h"
#include "solution.h"
#include "problem.h"
#include "solver_wrapper.h"
#include "gexf_writer.h"
#include "route_validator.h"


static const int STATUS_ERROR = 1;
static const int STATUS_OK = 1;

DEFINE_string(problem_file, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem_file, &util::ValidateFilePath);

DEFINE_string(solution_file, "", "a file path to the solution file");
DEFINE_validator(solution_file, &util::TryValidateFilePath);

DEFINE_string(map_file, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(map_file, &util::ValidateFilePath);

rows::Problem LoadReducedProblem(const std::string &problem_path) {
    boost::filesystem::path problem_file(boost::filesystem::canonical(FLAGS_problem_file));
    std::ifstream problem_stream;
    problem_stream.open(problem_file.c_str());
    if (!problem_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     STATUS_ERROR);
    }

    nlohmann::json problem_json;
    try {
        problem_stream >> problem_json;
    } catch (...) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     boost::current_exception_diagnostic_information(),
                                     STATUS_ERROR);
    }

    rows::Problem problem;
    try {
        rows::Problem::JsonLoader json_loader;
        problem = json_loader.Load(problem_json);
    } catch (const std::domain_error &ex) {
        throw util::ApplicationError(
                (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % problem_file % ex.what()).str(),
                STATUS_ERROR);
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

rows::Solution LoadSolution(const std::string &solution_path, const rows::Problem &problem) {
    boost::filesystem::path solution_file(boost::filesystem::canonical(FLAGS_solution_file));
    std::ifstream solution_stream;
    solution_stream.open(solution_file.c_str());
    if (!solution_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                     STATUS_ERROR);
    }

    nlohmann::json solution_json;
    try {
        solution_stream >> solution_json;
    } catch (...) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                     boost::current_exception_diagnostic_information(),
                                     STATUS_ERROR);
    }

    try {
        rows::Solution::JsonLoader json_loader;
        auto original_solution = json_loader.Load(solution_json);
        const auto time_span = problem.Timespan();
        return original_solution.Trim(time_span.first, time_span.second - time_span.first);
    } catch (const std::domain_error &ex) {
        throw util::ApplicationError(
                (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % solution_file % ex.what()).str(),
                STATUS_ERROR);
    }
}

osrm::EngineConfig CreateEngineConfig(const std::string &maps_file) {
    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig(maps_file);
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    if (!config.IsValid()) {
        throw util::ApplicationError("Invalid Open Street Map engine configuration", 1);
    }

    return config;
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);

    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling");
    static const bool REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\nproblem_file: %1%\nsolution_file: %2%\nmap_file: %3%")
               % FLAGS_problem_file
               % FLAGS_solution_file
               % FLAGS_map_file;

    try {
        boost::optional<rows::Solution> solution;

        auto problem_to_use = LoadReducedProblem(FLAGS_problem_file);

        if (!FLAGS_solution_file.empty()) {
            solution = LoadSolution(FLAGS_solution_file, problem_to_use);
            solution.get().UpdateVisitLocations(problem_to_use.visits());
            problem_to_use.RemoveCancelled(solution.get().visits());
        }

        auto engine_config = CreateEngineConfig(FLAGS_map_file);
        rows::SolverWrapper wrapper(problem_to_use, engine_config);

        operations_research::RoutingModel model{wrapper.NodesCount(),
                                                wrapper.VehicleCount(),
                                                rows::SolverWrapper::DEPOT};
        wrapper.ConfigureModel(model);
        operations_research::Assignment const *assignment = nullptr;
        if (solution) {
            solution.get().DebugPrintRoutes(wrapper, model);
            const auto solution_to_use = wrapper.ResolveValidationErrors(solution.get(), problem_to_use, model);
            solution_to_use.DebugPrintRoutes(wrapper, model);

            for (const auto &visit : solution_to_use.visits()) {
                if (visit.carer().is_initialized()) {
                    LOG(INFO) << visit;
                }
            }

            const auto routes = wrapper.GetNodeRoutes(solution_to_use, model);
            auto initial_assignment = model.ReadAssignmentFromRoutes(routes, false);
            if (!model.solver()->CheckAssignment(initial_assignment)) {
                throw util::ApplicationError("Solution for warm start is not valid.", STATUS_ERROR);
            }
            VLOG(1) << "No errors detected";
            assignment = model.SolveFromAssignmentWithParameters(initial_assignment, wrapper.parameters());
        } else {
            VLOG(1) << "Starting without solution";
            assignment = model.SolveWithParameters(wrapper.parameters());
        }

        if (assignment == nullptr) {
            throw util::ApplicationError("No solution found.", STATUS_ERROR);
        }

        rows::GexfWriter solution_writer;
        solution_writer.Write("../solution.gexf", wrapper, model, *assignment);

        wrapper.DisplayPlan(model,
                            *assignment,
                /*use_same_vehicle_costs=*/false,
                /*max_nodes_per_group=*/0,
                /*same_vehicle_cost=*/0,
                            model.GetDimensionOrDie(rows::SolverWrapper::TIME_DIMENSION));

        return STATUS_OK;
    } catch (util::ApplicationError &ex) {
        LOG(ERROR) << ex.msg() << std::endl << ex.diagnostic_info();
        return ex.exit_code();
    }
}