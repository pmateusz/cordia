#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <boost/filesystem.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include <osrm.hpp>
#include <osrm/storage_config.hpp>
#include <ortools/constraint_solver/routing_parameters.h>

#include "problem.h"
#include "single_step_solver.h"
#include "solution.h"
#include "solver_wrapper.h"
#include "util/aplication_error.h"
#include "util/logging.h"
#include "util/input.h"

TEST(RouteValidation, CanValidateRoute) {
    static const auto maps_path = "/home/pmateusz/dev/cordia/data/scotland-latest.osrm";
    static const auto problem_path = "/home/pmateusz/dev/cordia/problem.json";
    static const auto solution_path = "/home/pmateusz/dev/cordia/past_solution.json";
    static const rows::SimpleRouteValidatorWithTimeWindows validator{};
    static const auto TIME_WINDOW = boost::posix_time::minutes(90);

    auto printer = util::CreatePrinter(util::TEXT_FORMAT);
    auto problem = util::LoadReducedProblem(problem_path, "2017-10-04", printer);
    auto engine_config = util::CreateEngineConfig(maps_path);
    rows::RealProblemDataFactory problem_factory{engine_config};
    auto solution = util::LoadSolution(solution_path, problem, TIME_WINDOW);
    solution.UpdateVisitProperties(problem.visits());
    problem.RemoveCancelled(solution.visits());


    auto problem_data_ptr = problem_factory(problem);
    rows::SingleStepSolver wrapper(*problem_data_ptr, operations_research::DefaultRoutingSearchParameters());

    operations_research::RoutingIndexManager index_manager{wrapper.nodes(),
                                                           wrapper.vehicles(),
                                                           rows::ProblemData::DEPOT};

    operations_research::RoutingModel model(index_manager);
    std::shared_ptr<std::atomic<bool> > cancel_token = std::make_shared<std::atomic<bool> >(false);
    std::shared_ptr<rows::Printer> console_printer = std::make_shared<rows::ConsolePrinter>();
    wrapper.ConfigureModel(index_manager, model, console_printer, cancel_token);

    const auto route = solution.GetRoute(wrapper.Carer(0));

    // when
    const auto validation_result = validator.Validate(route, wrapper);
    ASSERT_FALSE(validation_result.error());
}

TEST(RouteValidation, ReproFullValidation) {
    using namespace boost::gregorian;
    using namespace boost::posix_time;

    const boost::gregorian::date day{2014, 10, 14};

    std::list<std::shared_ptr<rows::RouteValidatorBase::FixedDurationActivity> > activities{
            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "before working hours",
                    time_period(
                            ptime(day, time_duration(0, 0, 0)),
                            ptime(day, time_duration(0, 0, 0))),
                    time_duration(7, 15, 0),
                    rows::RouteValidatorBase::ActivityType::Break),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 360",
                    time_period(
                            ptime(day, time_duration(7, 15, 0)),
                            ptime(day, time_duration(8, 43, 31))),
                    time_duration(0, 21, 9),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 360-210",
                    time_period(
                            ptime(day, time_duration(7, 36, 9)),
                            ptime(day, time_duration(9, 4, 40))),
                    time_duration(0, 6, 1),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 210",
                    time_period(
                            ptime(day, time_duration(7, 42, 10)),
                            ptime(day, time_duration(9, 10, 41))),
                    time_duration(0, 30, 57),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 210-135",
                    time_period(
                            ptime(day, time_duration(8, 13, 7)),
                            ptime(day, time_duration(9, 41, 38))),
                    time_duration(0, 10, 43),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 135",
                    time_period(
                            ptime(day, time_duration(8, 45, 0)),
                            ptime(day, time_duration(9, 52, 21))),
                    time_duration(0, 16, 2),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 135-117",
                    time_period(
                            ptime(day, time_duration(9, 1, 2)),
                            ptime(day, time_duration(10, 8, 23))),
                    time_duration(0, 21, 37),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 117",
                    time_period(
                            ptime(day, time_duration(9, 22, 39)),
                            ptime(day, time_duration(10, 30, 0))),
                    time_duration(0, 20, 45),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 117-13",
                    time_period(
                            ptime(day, time_duration(9, 43, 24)),
                            ptime(day, time_duration(10, 50, 45))),
                    time_duration(0, 4, 45),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 13",
                    time_period(
                            ptime(day, time_duration(9, 48, 9)),
                            ptime(day, time_duration(11, 5, 55))),
                    time_duration(0, 24, 5),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 13-15",
                    time_period(
                            ptime(day, time_duration(10, 12, 14)),
                            ptime(day, time_duration(11, 30, 0))),
                    time_duration(),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 15",
                    time_period(
                            ptime(day, time_duration(11, 30, 0)),
                            ptime(day, time_duration(11, 30, 0))),
                    time_duration(0, 30, 0),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 15-16",
                    time_period(
                            ptime(day, time_duration(12, 0, 0)),
                            ptime(day, time_duration(12, 0, 0))),
                    time_duration(),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 16",
                    time_period(
                            ptime(day, time_duration(18, 0, 0)),
                            ptime(day, time_duration(18, 5, 6))),
                    time_duration(0, 14, 43),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 16-118",
                    time_period(
                            ptime(day, time_duration(18, 14, 43)),
                            ptime(day, time_duration(18, 19, 49))),
                    time_duration(0, 4, 45),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 118",
                    time_period(
                            ptime(day, time_duration(18, 19, 28)),
                            ptime(day, time_duration(18, 24, 34))),
                    time_duration(0, 12, 21),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 118-17",
                    time_period(
                            ptime(day, time_duration(18, 31, 49)),
                            ptime(day, time_duration(18, 36, 55))),
                    time_duration(0, 6, 50),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 17",
                    time_period(
                            ptime(day, time_duration(18, 38, 39)),
                            ptime(day, time_duration(18, 43, 45))),
                    time_duration(0, 14, 10),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 17-14",
                    time_period(
                            ptime(day, time_duration(18, 52, 49)),
                            ptime(day, time_duration(18, 57, 55))),
                    time_duration(0, 2, 5),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 14",
                    time_period(
                            ptime(day, time_duration(18, 54, 54)),
                            ptime(day, time_duration(19, 0, 0))),
                    time_duration(0, 9, 16),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 14-412",
                    time_period(
                            ptime(day, time_duration(19, 4, 10)),
                            ptime(day, time_duration(19, 9, 16))),
                    time_duration(0, 31, 17),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 412",
                    time_period(
                            ptime(day, time_duration(19, 35, 27)),
                            ptime(day, time_duration(19, 53, 46))),
                    time_duration(0, 15, 24),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 412-341",
                    time_period(
                            ptime(day, time_duration(19, 50, 51)),
                            ptime(day, time_duration(20, 9, 10))),
                    time_duration(0, 8, 41),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 341",
                    time_period(
                            ptime(day, time_duration(19, 59, 32)),
                            ptime(day, time_duration(20, 17, 51))),
                    time_duration(0, 11, 36),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 341-211",
                    time_period(
                            ptime(day, time_duration(20, 11, 8)),
                            ptime(day, time_duration(20, 29, 27))),
                    time_duration(0, 6, 58),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 211",
                    time_period(
                            ptime(day, time_duration(20, 18, 6)),
                            ptime(day, time_duration(20, 36, 25))),
                    time_duration(0, 18, 45),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Travel 211-136",
                    time_period(
                            ptime(day, time_duration(20, 36, 51)),
                            ptime(day, time_duration(20, 55, 10))),
                    time_duration(0, 19, 50),
                    rows::RouteValidatorBase::ActivityType::Travel),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "Visit 136",
                    time_period(
                            ptime(day, time_duration(20, 56, 41)),
                            ptime(day, time_duration(21, 15, 0))),
                    time_duration(0, 30, 0),
                    rows::RouteValidatorBase::ActivityType::Visit),

            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "after working hours",
                    time_period(
                            ptime(day, time_duration(21, 45, 0)),
                            ptime(boost::gregorian::date{2017, 10, 15}, time_duration(0, 0, 0))),
                    time_duration(2, 15, 0),
                    rows::RouteValidatorBase::ActivityType::Break)
    };
    std::vector<std::shared_ptr<rows::RouteValidatorBase::FixedDurationActivity> > breaks{
            std::make_shared<rows::RouteValidatorBase::FixedDurationActivity>(
                    "break 1",
                    time_period(
                            ptime(day, time_duration(9, 0, 0)),
                            ptime(day, time_duration(12, 0, 0))),
                    hours(6),
                    rows::RouteValidatorBase::ActivityType::Break)
    };

    rows::SolutionValidator validator;
    boost::posix_time::ptime start_time{day};
    ASSERT_TRUE(
            validator.is_schedule_valid(activities, breaks, start_time, std::begin(activities), std::begin(breaks)));
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