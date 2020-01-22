#include <thread>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <boost/format.hpp>

#include "util/input.h"
#include "util/logging.h"
#include "util/validation.h"

#include "three_step_worker.h"
#include "scheduling_worker.h"
#include "benchmark_problem_data.h"

DEFINE_string(problem, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists);

DEFINE_string(output, "output.gexf", "an output file");

void ParseArgs(int argc, char *argv[]) {
    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling\n"
                            "Example: rows-benchmark"
                            " --problem=problem.json");

    static const auto REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\n"
                             "problem: %1%") % FLAGS_problem;
}

int main(int argc, char *argv[]) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    auto problem_data_factory = rows::BenchmarkProblemDataFactory::Load(FLAGS_problem);
    auto problem_data_factory_ptr = std::make_shared<rows::BenchmarkProblemDataFactory>(problem_data_factory);
    auto problem_data_ptr = problem_data_factory_ptr->makeProblem();

    auto printer = util::CreatePrinter(util::LOG_FORMAT);
    rows::ThreeStepSchedulingWorker worker{std::move(printer),
                                           rows::FirstStageStrategy::TEAMS,
                                           rows::ThirdStageStrategy::DISTANCE,
                                           problem_data_factory_ptr};
    if (worker.Init(problem_data_ptr,
                    std::make_shared<rows::History>(),
                    FLAGS_output,
                    boost::posix_time::seconds(0),
                    boost::posix_time::seconds(0),
                    boost::posix_time::seconds(0),
                    boost::posix_time::seconds(5),
                    boost::posix_time::seconds(60),
                    boost::posix_time::minutes(60),
                    boost::posix_time::minutes(60),
                    problem_data_factory_ptr->CostNormalizationFactor())) {
        worker.Start();
        std::thread chat_thread(util::ChatBot<rows::SchedulingWorker>, std::ref(worker));
        chat_thread.detach();
        worker.Join();
    }
    return worker.ReturnCode();
}
