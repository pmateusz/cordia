#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include <memory>
#include <thread>
#include <future>
#include <iostream>
#include <regex>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/algorithm/string/join.hpp>
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
#include "printer.h"
#include "solver_wrapper.h"
#include "gexf_writer.h"
#include "single_step_solver.h"
#include "instant_transfer_solver.h"
#include "two_step_solver.h"
#include "multiple_carer_visit_constraint.h"
#include "scheduling_worker.h"
#include "two_step_worker.h"
#include "single_step_worker.h"
#include "incremental_worker.h"


DEFINE_string(problem, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists);

DEFINE_string(solution, "", "a file path to the solution file for warm start");
DEFINE_validator(solution, &util::file::IsNullOrExists);

DEFINE_string(maps, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(maps, &util::file::Exists);

static const std::string JSON_FORMAT{"json"};
static const std::string TEXT_FORMAT{"txt"};

bool ValidateConsoleFormat(const char *flagname, const std::string &value) {
    std::string value_to_use{value};
    util::string::Strip(value_to_use);
    util::string::ToLower(value_to_use);
    return value_to_use == JSON_FORMAT || value_to_use == TEXT_FORMAT;
}

DEFINE_string(console_format, "txt", "output format. Available options: txt or json");
DEFINE_validator(console_format, &ValidateConsoleFormat);

DEFINE_string(output, "solution.gexf", "a file path to save the solution");
DEFINE_validator(output, &util::file::IsNullOrNotExists);

DEFINE_string(time_limit, "", "total time dedicated for computation");
DEFINE_validator(time_limit, &util::time_duration::IsNullOrPositive);

static const auto DEFAULT_SOLUTION_LIMIT = std::numeric_limits<int64>::max();
DEFINE_int64(solutions_limit,
             DEFAULT_SOLUTION_LIMIT,
             "total number of solutions considered in the computation");
DEFINE_validator(solutions_limit, &util::numeric::IsPositive);

DEFINE_string(scheduling_date,
              "",
              "day to compute schedule for. By default it is the day of the earliest requested visit in the problem");
DEFINE_validator(scheduling_date, &util::date::IsNullOrPositive);

void ParseArgs(int argc, char **argv) {
    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling\n"
                            "Example: rows-main"
                            " --problem=problem.json"
                            " --maps=./data/scotland-latest.osrm"
                            " --solution=past_solution.json"
                            " --scheduling-date=2017-01-13"
                            " --output=solution.gexf"
                            " --time-limit=00:30:00"
                            " --solutions-limit=1024");

    FLAGS_output = util::file::GenerateNewFilePath("solution.gexf");

    static const auto REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\n"
                             "problem: %1%\n"
                             "maps: %2%\n"
                             "solution: %3%\n"
                             "scheduling-date: %4%\n"
                             "output: %5%\n"
                             "time-limit: %6%\n"
                             "solutions-limit: %7%")
               % FLAGS_problem
               % FLAGS_maps
               % FLAGS_solution
               % (FLAGS_scheduling_date.empty() ? "not set" : FLAGS_scheduling_date)
               % FLAGS_output
               % (FLAGS_time_limit.empty() ? "no" : FLAGS_time_limit)
               % FLAGS_solutions_limit;
}

rows::Problem LoadReducedProblem(std::shared_ptr<rows::Printer> printer) {
    boost::filesystem::path problem_file(boost::filesystem::canonical(FLAGS_problem));
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
                (boost::format("Failed to parse the file %1% due to error: '%2%'") % problem_file %
                 ex.what()).str(),
                util::ErrorCode::ERROR);
    }


    const std::pair<boost::posix_time::ptime, boost::posix_time::ptime> timespan_pair = problem.Timespan();
    const auto begin_date = timespan_pair.first.date();
    const auto end_date = timespan_pair.second.date();

    if (FLAGS_scheduling_date.empty()) {
        if (begin_date < end_date) {
            printer->operator<<(
                    (boost::format("Problem contains records from several days."
                                   " The computed solution will be reduced to a single day: '%1%'")
                     % begin_date).str());

            return problem.Trim(timespan_pair.first, boost::posix_time::hours(24));
        }

        return problem;
    }

    const boost::posix_time::ptime scheduling_time{boost::gregorian::from_simple_string(FLAGS_scheduling_date)};
    const auto scheduling_date = scheduling_time.date();
    if (begin_date == end_date && begin_date == scheduling_date) {
        return problem;
    } else if (begin_date <= scheduling_date && scheduling_date <= end_date) {
        return problem.Trim(scheduling_time, boost::posix_time::hours(24));
    } else {
        throw util::ApplicationError(
                (boost::format("Scheduling day '%1%' does not fin into the interval ['%2%','%3%']")
                 % scheduling_date
                 % timespan_pair.first
                 % timespan_pair.second).str(),
                util::ErrorCode::ERROR);
    }
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

void FailureInterceptor() {
    LOG(WARNING) << "here";
}

void ChatBot(rows::SchedulingWorker &worker) {
    std::regex non_printable_character_pattern{"[\\W]"};

    std::string line;
    while (true) {
        std::getline(std::cin, line);
        util::string::Strip(line);
        util::string::ToLower(line);

        if (!line.empty() && line == "stop") {
            worker.Cancel();
            break;
        }

        line.clear();
    }
}

std::shared_ptr<rows::Printer> CreatePrinter() {
    auto format_to_use = FLAGS_console_format;
    util::string::Strip(format_to_use);
    util::string::ToLower(format_to_use);
    if (format_to_use == JSON_FORMAT) {
        return std::make_shared<rows::JsonPrinter>();
    }

    if (format_to_use == TEXT_FORMAT) {
        return std::make_shared<rows::ConsolePrinter>();
    }

    throw util::ApplicationError("Unknown console format.", util::ErrorCode::ERROR);
}

rows::Solution LoadSolution(const std::string &solution_path, const rows::Problem &problem) {
    boost::filesystem::path solution_file(boost::filesystem::canonical(solution_path));
    std::ifstream solution_stream;
    solution_stream.open(solution_file.c_str());
    if (!solution_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                     util::ErrorCode::ERROR);
    }

    rows::Solution original_solution;
    const std::string file_extension{solution_file.extension().string()};
    if (file_extension == ".json") {
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
            original_solution = json_loader.Load(solution_json);
        } catch (const std::domain_error &ex) {
            throw util::ApplicationError(
                    (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % solution_file %
                     ex.what()).str(),
                    util::ErrorCode::ERROR);
        }
    } else if (file_extension == ".gexf") {
        rows::Solution::XmlLoader xml_loader;
        original_solution = xml_loader.Load(solution_file.string());
    } else {
        throw util::ApplicationError(
                (boost::format("Unknown file format: '%1%'. Use 'json' or 'gexf' format instead.")
                 % file_extension).str(), util::ErrorCode::ERROR);
    }

    const auto time_span = problem.Timespan();
    return original_solution.Trim(time_span.first, time_span.second - time_span.first);
}

int RunSingleStepSchedulingWorker() {
    std::shared_ptr<rows::Printer> printer = CreatePrinter();

    auto problem_to_use = LoadReducedProblem(printer);

    boost::optional<rows::Solution> solution;
    if (!FLAGS_solution.empty()) {
        solution = LoadSolution(FLAGS_solution, problem_to_use);
        solution.get().UpdateVisitProperties(problem_to_use.visits());
        problem_to_use.RemoveCancelled(solution.get().visits());
    }

    auto engine_config = CreateEngineConfig(FLAGS_maps);
    auto search_parameters = rows::SolverWrapper::CreateSearchParameters();
    if (FLAGS_solutions_limit != DEFAULT_SOLUTION_LIMIT) {
        search_parameters.set_solution_limit(FLAGS_solutions_limit);
    }

    if (!FLAGS_time_limit.empty()) {
        const auto duration_limit = boost::posix_time::duration_from_string(FLAGS_time_limit);
        search_parameters.set_time_limit_ms(duration_limit.total_milliseconds());
    }

    rows::SingleStepSchedulingWorker worker{printer};
    if (worker.Init(problem_to_use,
                    engine_config,
                    solution,
                    search_parameters,
                    FLAGS_output)) {

        worker.Start();
        std::thread chat_thread(ChatBot, std::ref(worker));
        chat_thread.detach();
        worker.Join();
    }

    return worker.ReturnCode();
}

int RunTwoStepSchedulingWorker() {
    std::shared_ptr<rows::Printer> printer = CreatePrinter();

    rows::TwoStepSchedulingWorker worker{printer};
    if (worker.Init(LoadReducedProblem(printer), CreateEngineConfig(FLAGS_maps))) {
        worker.Start();
        std::thread chat_thread(ChatBot, std::ref(worker));
        chat_thread.detach();
        worker.Join();
    }

    return worker.ReturnCode();
}

int RunIncrementalSchedulingWorker() {
    std::shared_ptr<rows::Printer> printer = CreatePrinter();

    rows::IncrementalSchedulingWorker worker{printer};
    if (worker.Init(LoadReducedProblem(printer),
                    CreateEngineConfig(FLAGS_maps),
                    rows::SolverWrapper::CreateSearchParameters(),
                    FLAGS_output)) {
        worker.Start();
        std::thread chat_thread(ChatBot, std::ref(worker));
        chat_thread.detach();
        worker.Join();
    }

    return worker.ReturnCode();
}

// TODO: enforce only percentage of constraints
// TODO: restart 5 times before stopping
// TODO: register previous and current lower bound and number of dropped visits

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    return RunIncrementalSchedulingWorker();
}