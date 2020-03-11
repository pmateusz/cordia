#include <utility>

#include <sstream>

#include <gurobi_c++.h>

#include <boost/exception/diagnostic_information.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <boost/config.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/routing_parameters.pb.h>
#include <ortools/constraint_solver/routing_parameters.h>

#include "util/input.h"
#include "util/logging.h"
#include "util/validation.h"

#include "location_container.h"
#include "solver_wrapper.h"
#include "estimate_solver.h"
#include "break.h"
#include "second_step_solver.h"
#include "gexf_writer.h"

DEFINE_string(problem,
              "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists
);

DEFINE_string(human_planners_solution,
              "", "a file path to the solution file for warm start");
DEFINE_validator(human_planners_solution, &util::file::Exists
);

DEFINE_string(solution,
              "", "a file path to the solution file for warm start");
DEFINE_validator(solution, &util::file::IsNullOrExists
);

DEFINE_string(maps,
              "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(maps, &util::file::Exists
);

DEFINE_string(output,
              "output.gexf", "an output file");

static const auto DEFAULT_TIME_LIMIT_TEXT = "00:03:00";
static const auto DEFAULT_TIME_LIMIT = boost::posix_time::duration_from_string(DEFAULT_TIME_LIMIT_TEXT);
DEFINE_string(time_limit, DEFAULT_TIME_LIMIT_TEXT,
              "time limit for proving the optimality");

DEFINE_double(gap_limit,
              0.001, "gap limit for proving the optimality");

DEFINE_string(break_time_window,
              "00:120:00",
              "Time window for breaks");
DEFINE_validator(break_time_window, &util::time_duration::IsNullOrPositive
);

DEFINE_string(visit_time_window,
              "00:120:00", "Time window for visits");
DEFINE_validator(visit_time_window, &util::time_duration::IsNullOrPositive
);

DEFINE_string(begin_end_shift_time_extension,
              "00:15:00",
              "Extra time added to the shift before and after working day");
DEFINE_validator(begin_end_shift_time_extension, &util::time_duration::IsNullOrPositive
);

void ParseArgs(int argc, char *argv[]) {
    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling\n"
                            "Example: rows-mip"
                            " --problem=problem.json"
                            " --maps=./data/scotland-latest.osrm");

    static const auto REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\n"
                             "problem: %1%\n"
                             "maps: %2%\n") % FLAGS_problem % FLAGS_maps;
}

int main(int argc, char *argv[]) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    const auto human_planner_schedule = util::LoadHumanPlannerSchedule(FLAGS_human_planners_solution);

    auto cancel_token = std::make_shared<std::atomic<bool> >(false);
    std::shared_ptr<rows::Printer> printer = util::CreatePrinter("log");
    const auto all_problem = util::LoadProblem(FLAGS_problem, printer);
    const auto problem = all_problem.Trim(boost::posix_time::ptime{human_planner_schedule.date()}, boost::posix_time::hours(24));

    const auto engine_config = util::CreateEngineConfig(FLAGS_maps);
    const auto visit_time_window = util::GetTimeDurationOrDefault(FLAGS_visit_time_window, boost::posix_time::not_a_date_time);
    const auto break_time_window = util::GetTimeDurationOrDefault(FLAGS_break_time_window, boost::posix_time::not_a_date_time);
    const auto begin_end_shift_time_extension
            = util::GetTimeDurationOrDefault(FLAGS_begin_end_shift_time_extension, boost::posix_time::not_a_date_time);

    operations_research::RoutingSearchParameters search_params = operations_research::DefaultRoutingSearchParameters();
//    search_params.set_first_solution_strategy(operations_research::FirstSolutionStrategy::ALL_UNPERFORMED);
//    search_params.mutable_local_search_operators()->set_use_exchange_subtrip(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_relocate_expensive_chain(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_light_relocate_pair(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_relocate(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_exchange(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_exchange_pair(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_extended_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_node_pair_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_cross_exchange(operations_research::OptionalBoolean::BOOL_TRUE);
//    search_params.mutable_local_search_operators()->set_use_relocate_neighbors(operations_research::OptionalBoolean::BOOL_TRUE);

//    search_params.set_local_search_metaheuristic(operations_research::LocalSearchMetaheuristic_Value_GUIDED_LOCAL_SEARCH);
//    search_params.set_guided_local_search_lambda_coefficient(1.0);

    auto problem_data_factory_ptr = std::make_shared<rows::RealProblemDataFactory>(engine_config);
    const auto problem_data = problem_data_factory_ptr->makeProblem(problem);
    rows::EstimateSolver solver{*problem_data,
                                human_planner_schedule,
                                search_params,
                                util::GetTimeDurationOrDefault(FLAGS_visit_time_window, boost::posix_time::not_a_date_time),
                                util::GetTimeDurationOrDefault(FLAGS_break_time_window, boost::posix_time::not_a_date_time),
                                util::GetTimeDurationOrDefault(FLAGS_begin_end_shift_time_extension, boost::posix_time::not_a_date_time),
                                boost::posix_time::seconds(30)};

    operations_research::RoutingModel model{solver.index_manager()};
//    second_stage_model->solver()->set_fail_intercept(&FailureInterceptor);
    solver.ConfigureModel(model, printer, cancel_token, 1.0);

    printer->operator<<(rows::TracingEvent(rows::TracingEventType::Started, "Stage1"));
    const auto solution_assignment = model.SolveWithParameters(search_params);
    printer->operator<<(rows::TracingEvent(rows::TracingEventType::Finished, "Stage1"));

    if (solution_assignment == nullptr) {
        throw util::ApplicationError("No second stage solution found.", util::ErrorCode::ERROR);
    }

    // TODO: save solution

    return EXIT_SUCCESS;
}