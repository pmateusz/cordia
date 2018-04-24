#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include <osrm.hpp>
#include <osrm/storage_config.hpp>
#include <util/logging.h>
#include <single_step_solver.h>

#include "solution.h"
#include "problem.h"
#include "solver_wrapper.h"
#include "util/aplication_error.h"


rows::Solution LoadSolution(const std::string &solution_path, const rows::Problem &problem) {
    boost::filesystem::path solution_file(boost::filesystem::canonical(solution_path));
    std::ifstream solution_stream;
    solution_stream.open(solution_file.c_str());
    if (!solution_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                     util::ErrorCode::ERROR);
    }

    nlohmann::json solution_json;
    try {
        solution_stream >> solution_json;
    } catch (...) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                     boost::current_exception_diagnostic_information(),
                                     util::ErrorCode::ERROR);
    }

    try {
        rows::Solution::JsonLoader json_loader;
        auto original_solution = json_loader.Load(solution_json);
        const auto time_span = problem.Timespan();
        return original_solution.Trim(time_span.first, time_span.second - time_span.first);
    } catch (const std::domain_error &ex) {
        throw util::ApplicationError(
                (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % solution_file % ex.what()).str(),
                util::ErrorCode::ERROR);
    }
}

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

osrm::EngineConfig CreateEngineConfig(const std::string &maps_file) {
    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig(maps_file);
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    if (!config.IsValid()) {
        throw util::ApplicationError("Invalid Open Street Map engine configuration", util::ErrorCode::ERROR);
    }

    return config;
}

TEST(RouteValidation, CanValidateRoute) {
    static const auto maps_path = "/home/pmateusz/dev/cordia/data/scotland-latest.osrm";
    static const auto problem_path = "/home/pmateusz/dev/cordia/problem.json";
    static const auto solution_path = "/home/pmateusz/dev/cordia/past_solution.json";
    static const rows::SimpleRouteValidatorWithTimeWindows validator{};

    auto problem = LoadReducedProblem(problem_path);
    auto solution = LoadSolution(solution_path, problem);
    solution.UpdateVisitProperties(problem.visits());
    problem.RemoveCancelled(solution.visits());

    auto engine_config = CreateEngineConfig(maps_path);
    rows::SingleStepSolver wrapper(problem, engine_config, rows::SolverWrapper::CreateSearchParameters());

    operations_research::RoutingModel model{wrapper.nodes(),
                                            wrapper.vehicles(),
                                            rows::SolverWrapper::DEPOT};

    std::atomic<bool> cancel_token{false};
    std::shared_ptr<rows::Printer> console_printer = std::make_shared<rows::ConsolePrinter>();
    wrapper.ConfigureModel(model, console_printer, cancel_token);

    const auto route = solution.GetRoute(wrapper.Carer(0));

    // when
    const auto validation_result = validator.Validate(route, wrapper);
    ASSERT_FALSE(validation_result.error());
}

int main(int argc, char **argv) {
    FLAGS_minloglevel = google::GLOG_INFO;
    FLAGS_v = 2;
    util::SetupLogging(argv[0]);

    ::testing::InitGoogleTest(&argc, argv);
    auto result = RUN_ALL_TESTS();

    google::FlushLogFiles(google::GLOG_INFO);
    return result;
}