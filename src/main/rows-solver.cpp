#include "boost/filesystem.hpp"
#include "gflags/gflags.h"
#include "glog/logging.h"

#include "nlohmann/json.hpp"

#include "util/logging.h"

DEFINE_string(problem_instance,
              boost::filesystem::absolute("problem.json").native(),
              "a file path to the problem instance");

int main(int argc, char **argv) {
    static const auto REMOVE_FLAGS = false;

    util::SetupLogging(argv[0]);

    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling");
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    LOG(INFO) << "Launched with problem file: " << FLAGS_problem_instance;

    std::ifstream problem_instance_file(FLAGS_problem_instance);
    nlohmann::json json;
    problem_instance_file >> json;

    std::cout << json;
}