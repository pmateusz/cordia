#include <string>
#include <functional>
#include <ostream>
#include <vector>

#include <boost/date_time.hpp>
#include <boost/format.hpp>

#include <gtest/gtest.h>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/routing_flags.h>


struct Visit {
    Visit(std::size_t location, const std::string &begin, const std::string &end, const std::string &duration)
            : Location{location},
              Begin{boost::posix_time::duration_from_string(begin)},
              End{boost::posix_time::duration_from_string(end)},
              Duration{boost::posix_time::duration_from_string(duration)} {}

    const std::size_t Location;
    const boost::posix_time::time_duration Begin;
    const boost::posix_time::time_duration End;
    const boost::posix_time::time_duration Duration;
};

std::ostream &operator<<(std::ostream &out, const Visit &visit) {
    out << boost::format("%1% [%2%,%3%] %4%")
           % visit.Location
           % visit.Begin
           % visit.End
           % visit.Duration;
    return out;
}

struct Break {
    Break(const std::string &start, const std::string &duration)
            : Start{boost::posix_time::duration_from_string(start)},
              Duration{boost::posix_time::duration_from_string(duration)} {}

    const boost::posix_time::time_duration Start;
    const boost::posix_time::time_duration Duration;
};

struct Environment {
    static const std::string TIME_DIM;

    Environment(std::vector<Visit> visits,
                std::vector<std::vector<Break> > breaks,
                std::vector<std::vector<int64> > distances)
            : Depot{0},
              Visits(std::move(visits)),
              Breaks(std::move(breaks)),
              Distances(std::move(distances)) {}

    int64 distance(operations_research::RoutingModel::NodeIndex from_node,
                   operations_research::RoutingModel::NodeIndex to_node) const {
        if (from_node == Depot || to_node == Depot) {
            return 0;
        }

        const auto from = NodeToVisit(from_node).Location;
        const auto to = NodeToVisit(to_node).Location;
        return Distances.at(from).at(to);
    }

    int64 service_plus_distance(operations_research::RoutingModel::NodeIndex from_node,
                                operations_research::RoutingModel::NodeIndex to_node) const {
        if (from_node == Depot) {
            return 0;
        }

        const auto service_time = NodeToVisit(from_node).Duration.total_seconds();
        return service_time + distance(from_node, to_node);
    }

    const Visit &NodeToVisit(operations_research::RoutingModel::NodeIndex node) const {
        return Visits.at(static_cast<std::size_t>(node.value() - 1));
    }

    const operations_research::RoutingModel::NodeIndex Depot;
    const std::vector<Visit> Visits;
    const std::vector<std::vector<Break> > Breaks;
    const std::vector<std::vector<int64> > Distances;
};

const std::string Environment::TIME_DIM{"time"};

static const Environment DATA{
        std::vector<Visit>{
                {4, "07:30:00", "08:30:00", "00:30:00"},
                {4, "12:00:00", "13:00:00", "00:30:00"},
                {6, "16:00:00", "17:00:00", "01:00:00"},
                {6, "18:30:00", "19:30:00", "01:00:00"},
                {7, "19:00:00", "20:00:00", "00:45:00"},
                {8, "07:30:00", "08:30:00", "01:00:00"},// second vehicle starts
                {2, "11:00:00", "12:00:00", "00:30:00"},
                {8, "12:00:00", "13:00:00", "00:30:00"},
                {3, "12:30:00", "13:30:00", "00:30:00"},
                {1, "13:00:00", "14:00:00", "00:30:00"},
                {5, "16:00:00", "17:00:00", "00:30:00"},
                {0, "17:00:00", "18:00:00", "00:30:00"},
                {2, "18:30:00", "19:30:00", "00:30:00"},
                {8, "19:00:00", "20:00:00", "00:30:00"},
                {2, "19:45:00", "20:45:00", "00:30:00"}
        },
        std::vector<std::vector<Break> >{
                {
                        {"00:00:00", "08:00:00"},
                        {"13:30:00", "16:30:00"},
                        {"19:00:00", "20:00:00"},
                        {"22:00:00", "24:00:00"},
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"11:00:00", "11:30:00"},
                        {"13:30:00", "16:30:00"},
                        {"19:00:00", "19:30:00"},
                        {"22:00:00", "24:00:00"}
                }
        },
        std::vector<std::vector<int64>>{
                {0,    909,  1386, 1129, 1414, 819,  1618, 1107, 1265},
                {909,  0,    546,  446,  1100, 277,  1304, 1171, 461},
                {1386, 546,  0,    429,  1069, 636,  1216, 1140, 227},
                {1129, 446,  429,  0,    655,  722,  859,  726,  295},
                {1414, 1100, 1069, 655,  0,    1376, 205,  550,  934},
                {819,  277,  636,  722,  1376, 0,    1581, 1305, 644},
                {1618, 1304, 1216, 859,  205,  1581, 0,    755,  1139},
                {1107, 1171, 1140, 726,  550,  1305, 755,  0,    1006},
                {1265, 461,  227,  295,  934,  644,  1139, 1006, 0}
        }};

TEST(TestBreaksViolation, FindsValidSolution) {
    // given
    operations_research::RoutingModel model(static_cast<int>(DATA.Visits.size() + 1),
                                            static_cast<int>(DATA.Breaks.size()),
                                            DATA.Depot);

    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(&DATA, &Environment::distance));

    static const auto FIX_CUMULATIVE_TO_ZERO = true;
    static const auto MAX_TIME_SLACK = boost::posix_time::hours(24).total_seconds();
    static const auto CAPACITY = boost::posix_time::hours(24).total_seconds();
    model.AddDimension(NewPermanentCallback(&DATA, &Environment::service_plus_distance),
                       MAX_TIME_SLACK,
                       CAPACITY,
                       FIX_CUMULATIVE_TO_ZERO,
                       Environment::TIME_DIM);

    auto time_dimension = model.GetMutableDimension(Environment::TIME_DIM);

    for (auto visit_node = DATA.Depot + 1; visit_node < model.nodes(); ++visit_node) {
        const auto &visit = DATA.NodeToVisit(visit_node);
        const auto visit_index = model.NodeToIndex(visit_node);

        time_dimension->CumulVar(visit_index)->SetRange(visit.Begin.total_seconds(), visit.End.total_seconds());
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(visit_index));
        model.AddToAssignment(time_dimension->SlackVar(visit_index));

        static const auto DROP_PENALTY = 1000000;
        model.AddDisjunction({visit_node}, DROP_PENALTY);
    }

    for (auto variable_index = 0; variable_index < model.Size(); ++variable_index) {
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(variable_index));
    }

    for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        std::vector<operations_research::IntervalVar *> break_intervals;
        auto break_index = 0;
        for (const auto &break_config : DATA.Breaks[vehicle]) {
            auto interval = model.solver()->MakeFixedInterval(break_config.Start.total_seconds(),
                                                              break_config.Duration.total_seconds(),
                                                              (boost::format("Break %1% of vehicle %2%")
                                                               % break_index
                                                               % vehicle).str());
            break_intervals.emplace_back(interval);
        }

        time_dimension->SetBreakIntervalsOfVehicle(std::move(break_intervals), vehicle);
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(model.Start(vehicle)));
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(model.End(vehicle)));
    }

    operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
    parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
    parameters.set_solution_limit(16);
    parameters.set_time_limit_ms(boost::posix_time::minutes(3).total_milliseconds());

    static const auto USE_ADVANCED_SEARCH = true;
    parameters.set_use_light_propagation(USE_ADVANCED_SEARCH);
    parameters.mutable_local_search_operators()->set_use_path_lns(USE_ADVANCED_SEARCH);
    parameters.mutable_local_search_operators()->set_use_inactive_lns(USE_ADVANCED_SEARCH);

    model.CloseModelWithParameters(parameters);

    const operations_research::Assignment *solution = model.SolveWithParameters(parameters);
    ASSERT_TRUE(nullptr == solution);


    // next test
    std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > routes{
            std::vector<operations_research::RoutingModel::NodeIndex>{
                    operations_research::RoutingModel::NodeIndex{1},
                    operations_research::RoutingModel::NodeIndex{2},
                    operations_research::RoutingModel::NodeIndex{3},
                    operations_research::RoutingModel::NodeIndex{4},
                    operations_research::RoutingModel::NodeIndex{5}},
            std::vector<operations_research::RoutingModel::NodeIndex>{
                    operations_research::RoutingModel::NodeIndex{6},
                    operations_research::RoutingModel::NodeIndex{7},
                    operations_research::RoutingModel::NodeIndex{8},
                    operations_research::RoutingModel::NodeIndex{9},
                    operations_research::RoutingModel::NodeIndex{10},
                    operations_research::RoutingModel::NodeIndex{11},
                    operations_research::RoutingModel::NodeIndex{12},
                    operations_research::RoutingModel::NodeIndex{13},
                    operations_research::RoutingModel::NodeIndex{14},
                    operations_research::RoutingModel::NodeIndex{15}
            }
    };

    auto initial_assignment = model.ReadAssignmentFromRoutes(routes, false);
    EXPECT_TRUE(initial_assignment == nullptr);
}