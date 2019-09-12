#include <string>
#include <vector>
#include <algorithm>
#include <utility>
#include <memory>
#include <thread>
#include <future>
#include <iostream>
#include <regex>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <absl/time/time.h>
#include <ortools/base/protoutil.h>

#include <osrm/engine/engine_config.hpp>

#include <ortools/constraint_solver/routing_parameters.h>

#include "util/logging.h"
#include "util/validation.h"
#include "util/input.h"
#include "event.h"
#include "solution.h"
#include "problem.h"
#include "printer.h"
#include "scheduling_worker.h"
#include "three_step_worker.h"
#include "single_step_worker.h"

DEFINE_string(problem, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists);

DEFINE_string(solution, "", "a file path to the solution file for warm start");
DEFINE_validator(solution, &util::file::IsNullOrExists);

DEFINE_string(maps, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(maps, &util::file::Exists);

DEFINE_string(console_format, "txt", "output format. Available options: txt, json or log");
DEFINE_validator(console_format, &util::ValidateConsoleFormat);

static const auto DEFAULT_SOLUTION_LIMIT = std::numeric_limits<int64>::max();
DEFINE_int64(solutions_limit,
             DEFAULT_SOLUTION_LIMIT,
             "total number of solutions considered in the computation");
DEFINE_validator(solutions_limit, &util::numeric::IsPositive);

DEFINE_string(scheduling_date,
              "",
              "day to compute schedule for. By default it is the day of the earliest requested visit in the problem");
DEFINE_validator(scheduling_date, &util::date::IsNullOrPositive);

DEFINE_string(preopt_noprogress_time_limit,
              "00:01:00",
              "Stop pre-optimization if no better solution was found after given time");
DEFINE_validator(preopt_noprogress_time_limit, &util::time_duration::IsNullOrPositive);

DEFINE_string(opt_noprogress_time_limit,
              "00:05:00",
              "Stop optimization if no better solution was found after given time");
DEFINE_validator(opt_noprogress_time_limit, &util::time_duration::IsNullOrPositive);

DEFINE_string(postopt_noprogress_time_limit,
              "00:05:00",
              "Stop post-optimization if no better solution was found after given time");
DEFINE_validator(postopt_noprogress_time_limit, &util::time_duration::IsNullOrPositive);

DEFINE_string(break_time_window,
              "00:120:00",
              "Time window for breaks");
DEFINE_validator(break_time_window, &util::time_duration::IsNullOrPositive);

DEFINE_string(visit_time_window, "00:120:00", "Time window for visits");
DEFINE_validator(visit_time_window, &util::time_duration::IsNullOrPositive);

DEFINE_string(begin_end_shift_time_extension,
              "00:15:00",
              "Extra time added to the shift before and after working day");
DEFINE_validator(begin_end_shift_time_extension, &util::time_duration::IsNullOrPositive);

DEFINE_bool(solve_all, false, "solve the scheduling problem for all instances");

DEFINE_string(output, "solution.gexf", "a file path to save the solution");
DEFINE_validator(output, &util::file::IsNullOrNotExists);

DEFINE_string(output_prefix, "solution", "a prefix that is added to the output file with a solution");

bool ValidateFirstStage(const char *flagname, const std::string &value) {
    return static_cast<bool>(rows::ParseFirstStageStrategy(value));
}

bool ValidateThirdStage(const char *flagname, const std::string &value) {
    return static_cast<bool>(rows::ParseThirdStageStrategy(value));
}

DEFINE_string(first_stage,
              "default",
              "a formulation used to compute schedule."
              " Available options for this setting are: teams, soft-windows and none");
DEFINE_validator(first_stage, &ValidateFirstStage);

DEFINE_string(third_stage,
              "default",
              "a formulation used to compute schedule."
              " Available options for this setting are: reduction, distance and none");
DEFINE_validator(third_stage, &ValidateThirdStage);

inline std::string FlagOrDefaultValue(const std::string &flag_value, const std::string &default_value) {
    if (flag_value.empty()) { return default_value; }
    return flag_value;
}

const std::string YES_OPTION{"yes"};
const std::string NO_OPTION{"no"};

inline const std::string &GetYesOrNoOption(bool value) {
    if (value) { return YES_OPTION; }
    return NO_OPTION;
}

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
                             "visit-time-window: %6%\n"
                             "break-time-window: %7%\n"
                             "begin-end-shift-time-adjustment: %8%\n"
                             "pre-opt-time-limit: %9%\n"
                             "opt-time-limit: %10%\n"
                             "post-opt-time-limit: %11%\n"
                             "solutions-limit: %12%\n"
                             "solve-all: %13%")
               % FLAGS_problem
               % FLAGS_maps
               % FLAGS_solution
               % FlagOrDefaultValue(FLAGS_scheduling_date, "not set")
               % FLAGS_output
               % FlagOrDefaultValue(FLAGS_visit_time_window, "no")
               % FlagOrDefaultValue(FLAGS_break_time_window, "no")
               % FlagOrDefaultValue(FLAGS_begin_end_shift_time_extension, "no")
               % FlagOrDefaultValue(FLAGS_preopt_noprogress_time_limit, "no")
               % FlagOrDefaultValue(FLAGS_opt_noprogress_time_limit, "no")
               % FlagOrDefaultValue(FLAGS_postopt_noprogress_time_limit, "no")
               % FLAGS_solutions_limit
               % GetYesOrNoOption(FLAGS_solve_all);
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

int RunSingleStepSchedulingWorker() {
    std::shared_ptr<rows::Printer> printer = util::CreatePrinter(FLAGS_console_format);

    auto problem_to_use = util::LoadReducedProblem(FLAGS_problem, FLAGS_scheduling_date, printer);

    boost::optional<rows::Solution> solution;
    if (!FLAGS_solution.empty()) {
        static const boost::posix_time::time_duration ZERO_DURATION;
        solution = util::LoadSolution(FLAGS_solution, problem_to_use, ZERO_DURATION);
        solution.get().UpdateVisitProperties(problem_to_use.visits());
        problem_to_use.RemoveCancelled(solution.get().visits());
    }

    auto search_parameters = operations_research::DefaultRoutingSearchParameters();
    auto engine_config = util::CreateEngineConfig(FLAGS_maps);
    if (FLAGS_solutions_limit != DEFAULT_SOLUTION_LIMIT) {
        search_parameters.set_solution_limit(FLAGS_solutions_limit);
    }

    if (!FLAGS_opt_noprogress_time_limit.empty()) {
        const auto duration_limit = boost::posix_time::duration_from_string(FLAGS_opt_noprogress_time_limit);
        CHECK_OK(util_time::EncodeGoogleApiProto(
                absl::Milliseconds(duration_limit.total_milliseconds()),
                search_parameters.mutable_time_limit()));
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

int RunSchedulingWorker(std::shared_ptr<rows::Printer> printer,
                        const rows::FirstStageStrategy first_stage_strategy,
                        const rows::ThirdStageStrategy third_stage_strategy,
                        const rows::Problem &problem,
                        const std::string &output,
                        osrm::EngineConfig &engine_config,
                        const boost::posix_time::time_duration &visit_time_window,
                        const boost::posix_time::time_duration &break_time_window,
                        const boost::posix_time::time_duration &begin_end_shift_time_extension,
                        const boost::posix_time::time_duration &pre_opt_noprogress_time_limit,
                        const boost::posix_time::time_duration &opt_noprogress_time_limit,
                        const boost::posix_time::time_duration &post_opt_noprogress_time_limit) {
    if (first_stage_strategy != rows::FirstStageStrategy::NONE || third_stage_strategy != rows::ThirdStageStrategy::NONE) {
        rows::ThreeStepSchedulingWorker worker{std::move(printer), first_stage_strategy, third_stage_strategy};
        if (worker.Init(problem,
                        engine_config,
                        output,
                        visit_time_window,
                        break_time_window,
                        begin_end_shift_time_extension,
                        pre_opt_noprogress_time_limit,
                        opt_noprogress_time_limit,
                        post_opt_noprogress_time_limit)) {
            worker.Run();
        }
        return worker.ReturnCode();
    } else {
        rows::SingleStepSchedulingWorker worker{std::move(printer)};
        if (worker.Init(problem,
                        engine_config,
                        output,
                        visit_time_window,
                        break_time_window,
                        begin_end_shift_time_extension,
                        opt_noprogress_time_limit)) {
            worker.Run();
        }
        return worker.ReturnCode();
    }
}

int RunCancellableSchedulingWorker(std::shared_ptr<rows::Printer> printer,
                                   const rows::FirstStageStrategy &first_stage_strategy,
                                   const rows::ThirdStageStrategy &third_stage_strategy,
                                   const rows::Problem &problem,
                                   const std::string &output,
                                   osrm::EngineConfig &engine_config,
                                   const boost::posix_time::time_duration &visit_time_window,
                                   const boost::posix_time::time_duration &break_time_window,
                                   const boost::posix_time::time_duration &begin_end_shift_time_extension,
                                   const boost::posix_time::time_duration &pre_opt_noprogress_time_limit,
                                   const boost::posix_time::time_duration &opt_noprogress_time_limit,
                                   const boost::posix_time::time_duration &post_opt_noprogress_time_limit) {
    if (first_stage_strategy != rows::FirstStageStrategy::NONE || third_stage_strategy != rows::ThirdStageStrategy::NONE) {
        rows::ThreeStepSchedulingWorker worker{std::move(printer), first_stage_strategy, third_stage_strategy};
        if (worker.Init(problem,
                        engine_config,
                        output,
                        visit_time_window,
                        break_time_window,
                        begin_end_shift_time_extension,
                        pre_opt_noprogress_time_limit,
                        opt_noprogress_time_limit,
                        post_opt_noprogress_time_limit)) {
            worker.Start();
            std::thread chat_thread(ChatBot, std::ref(worker));
            chat_thread.detach();
            worker.Join();
        }
        return worker.ReturnCode();
    } else {
        rows::SingleStepSchedulingWorker worker{std::move(printer)};
        if (worker.Init(problem,
                        engine_config,
                        output,
                        visit_time_window,
                        break_time_window,
                        begin_end_shift_time_extension,
                        opt_noprogress_time_limit)) {

            worker.Start();
            std::thread chat_thread(ChatBot, std::ref(worker));
            chat_thread.detach();
            worker.Join();
        }
        return worker.ReturnCode();
    }
}

int RunSchedulingWorkerEx(const std::shared_ptr<rows::Printer> &printer,
                          const rows::FirstStageStrategy &first_stage_strategy,
                          const rows::ThirdStageStrategy &third_stage_strategy) {
    auto engine_config = util::CreateEngineConfig(FLAGS_maps);
    return RunCancellableSchedulingWorker(printer,
                                          first_stage_strategy,
                                          third_stage_strategy,
                                          util::LoadReducedProblem(FLAGS_problem, FLAGS_scheduling_date, printer),
                                          FLAGS_output,
                                          engine_config,
                                          util::GetTimeDurationOrDefault(FLAGS_visit_time_window, boost::posix_time::not_a_date_time),
                                          util::GetTimeDurationOrDefault(FLAGS_break_time_window, boost::posix_time::not_a_date_time),
                                          util::GetTimeDurationOrDefault(FLAGS_begin_end_shift_time_extension, boost::posix_time::not_a_date_time),
                                          util::GetTimeDurationOrDefault(FLAGS_preopt_noprogress_time_limit, boost::posix_time::not_a_date_time),
                                          util::GetTimeDurationOrDefault(FLAGS_opt_noprogress_time_limit, boost::posix_time::not_a_date_time),
                                          util::GetTimeDurationOrDefault(FLAGS_postopt_noprogress_time_limit, boost::posix_time::not_a_date_time));
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    const auto first_stage_strategy = rows::ParseFirstStageStrategy(FLAGS_first_stage).get();
    const auto third_stage_strategy = rows::ParseThirdStageStrategy(FLAGS_third_stage).get();

    std::shared_ptr<rows::Printer> printer = util::CreatePrinter(FLAGS_console_format);

    if (FLAGS_solve_all) {
        const auto problem = util::LoadProblem(FLAGS_problem, printer);

        std::unordered_set<boost::gregorian::date> scheduling_days;
        for (const auto &visit : problem.visits()) {
            scheduling_days.insert(visit.datetime().date());
        }

        std::vector<boost::gregorian::date> scheduling_days_in_order;
        scheduling_days_in_order.reserve(scheduling_days.size());
        std::copy(std::begin(scheduling_days),
                  std::end(scheduling_days),
                  std::back_inserter(scheduling_days_in_order));
        std::sort(std::begin(scheduling_days_in_order), std::end(scheduling_days_in_order));

        std::vector<rows::Problem> sub_problems;
        sub_problems.reserve(scheduling_days_in_order.size());
        for (const auto &date : scheduling_days_in_order) {
            sub_problems.push_back(problem.Trim(boost::posix_time::ptime(date, boost::posix_time::time_duration{}), boost::posix_time::hours(24)));
        }

        auto engine_config = util::CreateEngineConfig(FLAGS_maps);
        const auto visit_time_window = util::GetTimeDurationOrDefault(FLAGS_visit_time_window, boost::posix_time::not_a_date_time);
        const auto break_time_window = util::GetTimeDurationOrDefault(FLAGS_break_time_window, boost::posix_time::not_a_date_time);
        const auto begin_end_shift_time_extension = util::GetTimeDurationOrDefault(FLAGS_begin_end_shift_time_extension,
                                                                                   boost::posix_time::not_a_date_time);
        const auto pre_opt_no_progress_time_limit = util::GetTimeDurationOrDefault(FLAGS_preopt_noprogress_time_limit,
                                                                                   boost::posix_time::not_a_date_time);
        const auto opt_no_progress_time_limit = util::GetTimeDurationOrDefault(FLAGS_opt_noprogress_time_limit, boost::posix_time::not_a_date_time);
        const auto post_opt_no_progress_time_limit = util::GetTimeDurationOrDefault(FLAGS_postopt_noprogress_time_limit,
                                                                                    boost::posix_time::not_a_date_time);

        int problem_count = -1;
        std::vector<std::future<int> > compute_tasks;
        for (const auto &sub_problem : sub_problems) {
            problem_count++;
            if (sub_problem.visits().empty()) {
                std::promise<int> promise;
                promise.set_value(0);
                compute_tasks.push_back(promise.get_future());
                continue;
            }

            const auto scheduling_date = sub_problem.visits().begin()->datetime().date();
            const std::string output_file = (boost::format("%1%_%2%.gexf")
                                             % FLAGS_output_prefix
                                             % boost::gregorian::to_iso_string(scheduling_date)).str();
            std::packaged_task<int(std::shared_ptr<rows::Printer>,
                                   rows::FirstStageStrategy,
                                   rows::ThirdStageStrategy,
                                   const rows::Problem &,
                                   const std::string &,
                                   osrm::EngineConfig &,
                                   const boost::posix_time::time_duration,
                                   const boost::posix_time::time_duration,
                                   const boost::posix_time::time_duration,
                                   const boost::posix_time::time_duration,
                                   const boost::posix_time::time_duration,
                                   const boost::posix_time::time_duration)> compute_schedule(
                    RunSchedulingWorker);
            compute_schedule(printer,
                             first_stage_strategy,
                             third_stage_strategy,
                             sub_problem,
                             output_file,
                             engine_config,
                             visit_time_window,
                             break_time_window,
                             begin_end_shift_time_extension,
                             pre_opt_no_progress_time_limit,
                             opt_no_progress_time_limit,
                             post_opt_no_progress_time_limit);

            compute_tasks.push_back(compute_schedule.get_future());
        }
        DCHECK_EQ(compute_tasks.size(), sub_problems.size());

        for (std::size_t task_index = 0u; task_index < sub_problems.size(); ++task_index) {
            const auto return_code = compute_tasks[task_index].get();
            if (return_code != 0) {
                LOG(ERROR) << boost::format("Failed to compute scheduling for %1%. Return code: %2%")
                              % scheduling_days_in_order[task_index]
                              % return_code;
            }
        }

        return 0;
    } else {
        return RunSchedulingWorkerEx(printer, first_stage_strategy, third_stage_strategy);
    }
}