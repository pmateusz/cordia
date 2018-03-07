#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <memory>
#include <thread>
#include <future>
#include <iostream>
#include <regex>

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
#include "printer.h"
#include "solver_wrapper.h"
#include "gexf_writer.h"


static const int STATUS_OK = 1;
static const int NOT_STARTED = -1;

DEFINE_string(problem, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists);

DEFINE_string(solution, "", "a file path to the solution file for warm start");
DEFINE_validator(solution, &util::file::IsNullOrExists);

DEFINE_string(map, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(map, &util::file::Exists);

DEFINE_string(output, "solution.gexf", "a file path to save the solution");
DEFINE_validator(output, &util::file::IsNullOrNotExists);

DEFINE_string(time_limit, "", "total time dedicated for computation");
DEFINE_validator(time_limit, &util::date_time::IsNullOrPositive);

static const auto DEFAULT_SOLUTION_LIMIT = std::numeric_limits<int64>::max();
DEFINE_int64(solution_limit,
             DEFAULT_SOLUTION_LIMIT,
             "total number of solutions considered in the computation");
DEFINE_validator(solution_limit, &util::numeric::IsPositive);

void ParseArgs(int argc, char **argv) {
    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling\n"
                                    "Example: rows-main"
                                    " --problem=problem.json"
                                    " --map=./data/scotland-latest.osrm"
                                    " --solution=past_solution.json"
                                    " --output=solution.gexf"
                                    " --time-limit=00:30:00"
                                    " --solution-limit=1024");

    FLAGS_output = util::file::GenerateNewFilePath("solution.gexf");

    static const auto REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\n"
                                     "problem: %1%\n"
                                     "map: %2%\n"
                                     "past-solution: %3%\n"
                                     "output: %4%\n"
                                     "time-limit: %5%\n"
                                     "solution-limit: %6%")
               % FLAGS_problem
               % FLAGS_map
               % FLAGS_solution
               % FLAGS_output
               % (FLAGS_time_limit.empty() ? "no" : FLAGS_time_limit)
               % FLAGS_solution_limit;
}

class SchedulingWorker {
public:
    SchedulingWorker(std::shared_ptr<rows::Printer> printer)
            : printer_{std::move(printer)},
              initial_assignment_{nullptr},
              return_code_{NOT_STARTED},
              cancel_token_{false},
              worker_{} {}

    ~SchedulingWorker() {
        if (model_) {
            model_.reset();
        }

        if (wrapper_) {
            wrapper_.reset();
        }

        initial_assignment_ = nullptr;
    }

    bool Init(const std::string &problem_file,
              const std::string &map_file,
              const std::string &past_solution_file,
              const std::string &time_limit,
              int64 solution_limit) {
        try {
            auto problem_to_use = LoadReducedProblem(problem_file);

            boost::optional<rows::Solution> solution;
            if (!past_solution_file.empty()) {
                solution = LoadSolution(past_solution_file, problem_to_use);
                solution.get().UpdateVisitLocations(problem_to_use.visits());
                problem_to_use.RemoveCancelled(solution.get().visits());
            }

            auto engine_config = CreateEngineConfig(map_file);
            auto search_parameters = rows::SolverWrapper::CreateSearchParameters();

            if (solution_limit != DEFAULT_SOLUTION_LIMIT) {
                search_parameters.set_solution_limit(solution_limit);
            }

            if (!time_limit.empty()) {
                const auto duration_limit = boost::posix_time::duration_from_string(time_limit);
                search_parameters.set_time_limit_ms(duration_limit.total_milliseconds());
            }

            wrapper_ = std::make_unique<rows::SolverWrapper>(problem_to_use, engine_config, search_parameters);
            model_ = std::make_unique<operations_research::RoutingModel>(wrapper_->nodes(),
                                                                         wrapper_->vehicles(),
                                                                         rows::SolverWrapper::DEPOT);
//        model.solver()->parameters().set_print_added_constraints(true);
//        model.solver()->parameters().set_print_model(true);
//        model.solver()->parameters().set_print_model_stats(true);
//        model.solver()->parameters().disable_solve();

            wrapper_->ConfigureModel(*model_, printer_, cancel_token_);
            VLOG(1) << "Completed routing model configuration with status: " << GetModelStatus(model_->status());
            if (solution) {
                VLOG(1) << "Starting with a solution.";
                VLOG(1) << solution.get().DebugStatus(*wrapper_, *model_);
                const auto solution_to_use = wrapper_->ResolveValidationErrors(solution.get(), *model_);
                VLOG(1) << solution_to_use.DebugStatus(*wrapper_, *model_);

                if (VLOG_IS_ON(2)) {
                    for (const auto &visit : solution_to_use.visits()) {
                        if (visit.carer().is_initialized()) {
                            VLOG(2) << visit;
                        }
                    }
                }

                const auto routes = wrapper_->GetRoutes(solution_to_use, *model_);
                initial_assignment_ = model_->ReadAssignmentFromRoutes(routes, false);
                if (!model_->solver()->CheckAssignment(initial_assignment_)) {
                    throw util::ApplicationError("Solution for warm start is not valid.", util::ErrorCode::ERROR);
                }
            }
        } catch (util::ApplicationError &ex) {
            LOG(ERROR) << ex.msg() << std::endl << ex.diagnostic_info();
            return_code_ = util::to_exit_code(ex.error_code());
            return false;
        }
    }

    void Start(const std::string &output_file) {
        worker_ = std::thread(&SchedulingWorker::SolveScheduling, this, output_file);
    }

    void Join() {
        worker_.join();
    }

    void Cancel() {
        VLOG(1) << "Cancellation requested";
        cancel_token_ = true;
    }

    int ReturnCode() const {
        return return_code_;
    }

private:
    std::string GetModelStatus(int status) {
        switch (status) {
            case operations_research::RoutingModel::Status::ROUTING_FAIL:
                return "ROUTING_FAIL";
            case operations_research::RoutingModel::Status::ROUTING_FAIL_TIMEOUT:
                return "ROUTING_FAIL_TIMEOUT";
            case operations_research::RoutingModel::Status::ROUTING_INVALID:
                return "ROUTING_INVALID";
            case operations_research::RoutingModel::Status::ROUTING_NOT_SOLVED:
                return "ROUTING_NOT_SOLVED";
            case operations_research::RoutingModel::Status::ROUTING_SUCCESS:
                return "ROUTING_SUCCESS";
        }
    }

    rows::Problem LoadReducedProblem(const std::string &problem_path) {
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

        const auto timespan_pair = problem.Timespan();
        if (timespan_pair.first.date() < timespan_pair.second.date()) {
            printer_->operator<<(
                    (boost::format("Problem contains records from several days."
                                           " The computed solution will be reduced to a single day: '%1%'")
                     % timespan_pair.first.date()).str());
        }

        const auto problem_to_use = problem.Trim(timespan_pair.first, boost::posix_time::hours(24));
        DCHECK(problem_to_use.IsAdmissible());
        return problem_to_use;
    }

    rows::Solution LoadSolution(const std::string &solution_path, const rows::Problem &problem) {
        boost::filesystem::path solution_file(boost::filesystem::canonical(FLAGS_solution));
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
                    (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % solution_file %
                     ex.what()).str(),
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

    void SolveScheduling(const std::string &output_file) {
        try {
            if (initial_assignment_ == nullptr) {
                VLOG(1) << "Search started without a solution";
            } else {
                VLOG(1) << "Search started with a solution";
            }

            operations_research::Assignment const *assignment = model_->SolveFromAssignmentWithParameters(
                    initial_assignment_, wrapper_->parameters());

            VLOG(1) << "Search completed"
                    << "\nLocal search profile: " << model_->solver()->LocalSearchProfile()
                    << "\nDebug string: " << model_->solver()->DebugString()
                    << "\nModel status: " << GetModelStatus(model_->status());

            if (assignment == nullptr) {
                throw util::ApplicationError("No solution found.", util::ErrorCode::ERROR);
            }

            operations_research::Assignment validation_copy{assignment};
            const auto is_solution_correct = model_->solver()->CheckAssignment(&validation_copy);
            DCHECK(is_solution_correct);

            rows::GexfWriter solution_writer;
            solution_writer.Write(output_file, *wrapper_, *model_, *assignment);
            wrapper_->DisplayPlan(*model_, *assignment);
            return_code_ = STATUS_OK;
        } catch (util::ApplicationError &ex) {
            LOG(ERROR) << ex.msg() << std::endl << ex.diagnostic_info();
            return_code_ = util::to_exit_code(ex.error_code());
        } catch (const std::exception &ex) {
            LOG(ERROR) << ex.what() << std::endl;
            return_code_ = 2;
        } catch (...) {
            LOG(ERROR) << "Unhandled exception";
            return_code_ = 3;
        }
    }

    std::shared_ptr<rows::Printer> printer_;

    std::unique_ptr<operations_research::RoutingModel> model_;
    operations_research::Assignment *initial_assignment_;

    std::unique_ptr<rows::SolverWrapper> wrapper_;

    std::atomic<int> return_code_;
    std::atomic<bool> cancel_token_;
    std::thread worker_;
};

void ToLower(std::string input) {
    static const std::locale CURRENT_LOCALE("");

    std::transform(std::begin(input), std::end(input), std::begin(input),
                   [](auto character) {
                       return std::tolower(character, CURRENT_LOCALE);
                   });
}

void ChatBot(SchedulingWorker &worker) {
    std::regex non_printable_character_pattern{"[\\W]"};

    std::string line;
    while (true) {
        std::getline(std::cin, line);
        auto command = std::regex_replace(line, non_printable_character_pattern, "");
        ToLower(command);

        if (!command.empty() && command == "stop") {
            worker.Cancel();
            break;
        }

        line.clear();
    }
}

// TODO: develop console loggers for progress and information
int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    std::shared_ptr<rows::ConsolePrinter> printer = std::make_shared<rows::ConsolePrinter>();

    SchedulingWorker worker{printer};
    if (worker.Init(FLAGS_problem,
                    FLAGS_map,
                    FLAGS_solution,
                    FLAGS_time_limit,
                    FLAGS_solution_limit)) {

        worker.Start(FLAGS_output);
        std::thread chat_thread(ChatBot, std::ref(worker));
        chat_thread.detach();
        worker.Join();
    }

    return worker.ReturnCode();
}