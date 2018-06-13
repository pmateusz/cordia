#include <string>
#include <functional>
#include <ostream>
#include <vector>
#include <algorithm>

#include <nlohmann/json.hpp>

#include <boost/date_time.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>

#include <gtest/gtest.h>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/routing_flags.h>
#include <boost/exception/diagnostic_information.hpp>

#include <osrm/match_parameters.hpp>
#include <osrm/nearest_parameters.hpp>
#include <osrm/route_parameters.hpp>
#include <osrm/table_parameters.hpp>
#include <osrm/trip_parameters.hpp>
#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <osrm/osrm.hpp>

#include "util/aplication_error.h"
#include "util/error_code.h"
#include "problem.h"
#include "solution.h"

struct Visit {
    Visit(std::size_t location, const std::string &begin, const std::string &end, const std::string &duration)
            : Visit{location,
                    boost::posix_time::duration_from_string(begin),
                    boost::posix_time::duration_from_string(end),
                    boost::posix_time::duration_from_string(duration)} {}

    Visit(std::size_t location,
          boost::posix_time::time_duration begin,
          boost::posix_time::time_duration end,
          boost::posix_time::time_duration duration)
            : Location{location},
              Begin{begin},
              End{end},
              Duration{duration} {}

    const std::size_t Location;
    const boost::posix_time::time_duration Begin;
    const boost::posix_time::time_duration End;
    const boost::posix_time::time_duration Duration;
};

std::ostream &operator<<(std::ostream &out, const Visit &visit) {
    out << boost::format("%1% [%2%, %3%] %4%")
           % visit.Location
           % visit.Begin
           % visit.End
           % visit.Duration;
    return out;
}

struct Break {
    Break(const std::string &start, const std::string &duration)
            : Break{boost::posix_time::duration_from_string(start),
                    boost::posix_time::duration_from_string(duration)} {}

    Break(boost::posix_time::time_duration start, boost::posix_time::time_duration duration)
            : Start{start},
              Duration{duration} {}

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
        std::vector<std::vector<int64> >{
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

static const Environment REPRO{
        std::vector<Visit>{
                {0,  "09:00:00", "10:00:00", "00:45:00"},
                {0,  "09:00:00", "10:00:00", "00:45:00"},
                {0,  "12:15:00", "13:15:00", "00:45:00"},
                {0,  "12:15:00", "13:15:00", "00:45:00"},
                {0,  "16:30:00", "17:30:00", "00:45:00"},
                {0,  "16:30:00", "17:30:00", "00:45:00"},
                {0,  "20:00:00", "21:00:00", "00:30:00"},
                {0,  "20:00:00", "21:00:00", "00:30:00"},
                {1,  "09:30:00", "10:30:00", "00:30:00"},
                {2,  "08:45:00", "09:45:00", "00:15:00"},
                {3,  "07:00:00", "08:00:00", "01:00:00"},
                {3,  "07:00:00", "08:00:00", "01:00:00"},
                {4,  "09:30:00", "10:30:00", "00:30:00"},
                {4,  "17:30:00", "18:30:00", "00:30:00"},
                {4,  "19:30:00", "20:30:00", "00:30:00"},
                {5,  "08:15:00", "09:15:00", "00:15:00"},
                {5,  "17:00:00", "18:00:00", "00:30:00"},
                {3,  "08:45:00", "09:45:00", "00:30:00"},
                {3,  "12:15:00", "13:15:00", "00:30:00"},
                {3,  "16:30:00", "17:30:00", "00:15:00"},
                {3,  "18:30:00", "19:30:00", "00:15:00"},
                {6,  "08:00:00", "09:00:00", "00:30:00"},
                {6,  "19:30:00", "20:30:00", "00:30:00"},
                {7,  "09:00:00", "10:00:00", "00:30:00"},
                {7,  "12:30:00", "13:30:00", "00:30:00"},
                {7,  "16:30:00", "17:30:00", "00:30:00"},
                {7,  "09:00:00", "10:00:00", "00:45:00"},
                {7,  "12:00:00", "13:00:00", "00:30:00"},
                {7,  "17:00:00", "18:00:00", "00:30:00"},
                {7,  "18:45:00", "19:45:00", "00:30:00"},
                {8,  "08:00:00", "09:00:00", "00:30:00"},
                {8,  "11:00:00", "12:00:00", "01:00:00"},
                {8,  "16:15:00", "17:15:00", "00:15:00"},
                {8,  "19:30:00", "20:30:00", "00:15:00"},
                {9,  "07:30:00", "08:30:00", "00:45:00"},
                {9,  "11:30:00", "12:30:00", "00:30:00"},
                {9,  "16:45:00", "17:45:00", "00:30:00"},
                {9,  "19:00:00", "20:00:00", "00:30:00"},
                {0,  "08:30:00", "09:30:00", "00:30:00"},
                {0,  "12:30:00", "13:30:00", "00:30:00"},
                {0,  "16:30:00", "17:30:00", "00:15:00"},
                {0,  "19:30:00", "20:30:00", "00:30:00"},
                {10, "18:30:00", "19:30:00", "00:15:00"},
                {5,  "08:15:00", "09:15:00", "00:15:00"},
                {5,  "12:30:00", "13:30:00", "00:30:00"},
                {5,  "17:45:00", "18:45:00", "00:15:00"},
                {11, "14:45:00", "15:45:00", "00:30:00"},
                {12, "08:00:00", "09:00:00", "00:30:00"},
                {13, "09:00:00", "10:00:00", "00:30:00"},
                {14, "17:30:00", "18:30:00", "00:30:00"}
        },
        std::vector<std::vector<Break> >{
                {
                        {"00:00:00", "08:00:00"},
                        {"13:00:00", "03:00:00"},
                        {"21:00:00", "03:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"10:30:00", "01:30:00"},
                        {"14:00:00", "10:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"13:00:00", "11:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"11:00:00", "13:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"11:00:00", "00:30:00"},
                        {"13:30:00", "03:00:00"},
                        {"19:00:00", "00:30:00"},
                        {"22:00:00", "02:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"10:30:00", "01:30:00"},
                        {"14:00:00", "10:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"10:30:00", "01:30:00"},
                        {"14:00:00", "10:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"10:30:00", "01:30:00"},
                        {"14:00:00", "10:00:00"}
                },
                {
                        {"00:00:00", "09:00:00"},
                        {"11:00:00", "13:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"10:30:00", "01:30:00"},
                        {"14:00:00", "10:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"11:00:00", "00:30:00"},
                        {"13:30:00", "03:00:00"},
                        {"19:00:00", "00:30:00"},
                        {"22:00:00", "02:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"13:00:00", "11:00:00"}
                },
                {
                        {"00:00:00", "16:30:00"},
                        {"21:30:00", "02:30:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"11:00:00", "01:00:00"},
                        {"14:00:00", "10:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"11:00:00", "00:30:00"},
                        {"13:30:00", "03:00:00"},
                        {"19:00:00", "00:30:00"},
                        {"22:00:00", "02:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"14:00:00", "03:00:00"},
                        {"21:00:00", "03:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"10:30:00", "01:30:00"},
                        {"14:00:00", "10:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"10:30:00", "05:30:00"},
                        {"19:30:00", "00:30:00"},
                        {"22:00:00", "02:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"11:00:00", "00:30:00"},
                        {"13:30:00", "03:00:00"},
                        {"19:00:00", "00:30:00"},
                        {"22:00:00", "02:00:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"11:00:00", "00:30:00"},
                        {"13:30:00", "03:00:00"},
                        {"19:30:00", "00:30:00"},
                        {"22:00:00", "02:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"10:30:00", "06:00:00"},
                        {"21:30:00", "02:30:00"}
                },
                {
                        {"00:00:00", "08:00:00"},
                        {"11:00:00", "13:00:00"}
                },
                {
                        {"00:00:00", "07:30:00"},
                        {"10:30:00", "01:30:00"},
                        {"14:00:00", "10:00:00"}
                },
                {
                        {"00:00:00", "15:00:00"},
                        {"19:00:00", "05:00:00"}
                }
        },
        std::vector<std::vector<int64> >{
                {0,    722,  884,  604,  1562, 1129, 855,  655,  547,  432,  327,  945,  1170, 333,  517},
                {722,  0,    1455, 1006, 1944, 819,  1425, 1376, 1269, 291,  1048, 1516, 1184, 392,  425},
                {884,  1455, 0,    651,  2070, 1906, 229,  1083, 1140, 1173, 1134, 154,  1935, 1074, 1293},
                {604,  1006, 651,  0,    2089, 1611, 621,  1127, 1074, 742,  870,  712,  1713, 753,  1004},
                {1562, 1944, 2070, 2089, 0,    1509, 2186, 1146, 1015, 1993, 1322, 1942, 951,  1895, 1645},
                {1129, 819,  1906, 1611, 1509, 0,    1877, 1414, 1173, 1073, 1167, 1967, 623,  920,  690},
                {855,  1425, 229,  621,  2186, 1877, 0,    1224, 1171, 1143, 1106, 382,  1906, 1044, 1265},
                {655,  1376, 1083, 1127, 1146, 1414, 1224, 0,    241,  1086, 448,  955,  1090, 988,  1063},
                {547,  1269, 1140, 1074, 1015, 1173, 1171, 241,  0,    978,  333,  1012, 849,  880,  956},
                {432,  291,  1173, 742,  1993, 1073, 1143, 1086, 978,  0,    758,  1234, 1322, 194,  511},
                {327,  1048, 1134, 870,  1322, 1167, 1106, 448,  333,  758,  0,    1185, 844,  660,  735},
                {945,  1516, 154,  712,  1942, 1967, 382,  955,  1012, 1234, 1185, 0,    1832, 1136, 1355},
                {1170, 1184, 1935, 1713, 951,  623,  1906, 1090, 849,  1322, 844,  1832, 0,    1167, 885},
                {333,  392,  1074, 753,  1895, 920,  1044, 988,  880,  194,  660,  1136, 1167, 0,    330},
                {517,  425,  1293, 1004, 1645, 690,  1265, 1063, 956,  511,  735,  1355, 885,  330,  0}
        }
};

TEST(TestBreaksViolation, FindsValidSolution) {
    const auto &data_set = REPRO;
    // given
    operations_research::RoutingModel model(static_cast<int>(data_set.Visits.size() + 1),
                                            static_cast<int>(data_set.Breaks.size()),
                                            data_set.Depot);

    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(&data_set, &Environment::distance));

    static const auto FIX_CUMULATIVE_TO_ZERO = true;
    static const auto MAX_TIME_SLACK = boost::posix_time::hours(24).total_seconds();
    static const auto CAPACITY = boost::posix_time::hours(24).total_seconds();
    model.AddDimension(NewPermanentCallback(&data_set, &Environment::service_plus_distance),
                       MAX_TIME_SLACK,
                       CAPACITY,
                       FIX_CUMULATIVE_TO_ZERO,
                       Environment::TIME_DIM);

    auto time_dimension = model.GetMutableDimension(Environment::TIME_DIM);

    for (auto visit_node = data_set.Depot + 1; visit_node < model.nodes(); ++visit_node) {
        const auto &visit = data_set.NodeToVisit(visit_node);
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
        for (const auto &break_config : data_set.Breaks[vehicle]) {
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

    model.CloseModelWithParameters(parameters);

    const auto reference_date = boost::posix_time::second_clock::universal_time().date();
    const operations_research::Assignment *solution = model.SolveWithParameters(parameters);
    if (solution != nullptr) {
        operations_research::Assignment solution_to_check{solution};
        EXPECT_TRUE(model.solver()->CheckAssignment(&solution_to_check));

        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            std::vector<boost::posix_time::time_period> break_periods;
            const auto &vehicle_breaks = data_set.Breaks.at(static_cast<std::size_t>(vehicle));
            std::transform(std::begin(vehicle_breaks),
                           std::end(vehicle_breaks),
                           std::back_inserter(break_periods),
                           [&reference_date](const Break &vehicle_break) -> boost::posix_time::time_period {
                               return boost::posix_time::time_period(
                                       boost::posix_time::ptime(reference_date, vehicle_break.Start),
                                       vehicle_break.Duration);
                           });

            auto order = solution->Value(model.NextVar(model.Start(vehicle)));
            while (!model.IsEnd(order)) {
                const auto &visit = data_set.NodeToVisit(model.IndexToNode(order));
                const boost::posix_time::time_period min_period(
                        boost::posix_time::ptime(reference_date, boost::posix_time::seconds(
                                solution->Min(time_dimension->CumulVar(order)))), visit.Duration);

                const boost::posix_time::time_period max_period(
                        boost::posix_time::ptime(reference_date, boost::posix_time::seconds(
                                solution->Max(time_dimension->CumulVar(order)))), visit.Duration);

                for (const auto &break_period: break_periods) {
                    const auto intersection = break_period.intersection(min_period);
                    if (!intersection.is_null() && intersection.length() > boost::posix_time::seconds(1)) {
                        LOG(ERROR) << boost::format(
                                "Vehicle %1% break [%2%, %3%] overlaps with the time [%4%, %5%] allocated for the visit %6%")
                                      % vehicle
                                      % break_period.begin().time_of_day()
                                      % break_period.end().time_of_day()
                                      % min_period.begin().time_of_day()
                                      % min_period.end().time_of_day()
                                      % visit;
                    }

                    if (min_period != max_period) {
                        const auto second_intersection = break_period.intersection(max_period);
                        if (!second_intersection.is_null()
                            && second_intersection.length() > boost::posix_time::seconds(1)) {
                            LOG(ERROR) << boost::format(
                                    "Vehicle %1% break [%2%,%3%] overlaps with the time [%4%,%5%] allocated for the visit %6%")
                                          % vehicle
                                          % break_period.begin().time_of_day()
                                          % break_period.end().time_of_day()
                                          % max_period.begin().time_of_day()
                                          % max_period.end().time_of_day()
                                          % visit;
                        }
                    }
                }

                order = solution->Value(model.NextVar(order));
            }
        }
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

    return problem.Trim(timespan_pair.first, boost::posix_time::hours(24));
}

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

TEST(TestBreaksViolation, TestRealProblem) {
    static const auto TIME_WINDOW = boost::posix_time::minutes(30);
    auto problem = LoadReducedProblem("/home/pmateusz/dev/cordia/problem.json");
    auto solution = LoadSolution("/home/pmateusz/dev/cordia/past_solution.json", problem);

    solution.UpdateVisitProperties(problem.visits());
    problem.RemoveCancelled(solution.visits());

    auto current_index = 0;
    std::unordered_map<rows::Location, std::size_t> location_index;
    std::vector<rows::Location> locations;
    for (const auto &visit : problem.visits()) {
        const auto &location = visit.location().get();
        if (location_index.emplace(location, current_index).second) {
            locations.push_back(location);
            ++current_index;
        }
    }

    // initialize visits
    std::vector<Visit> visits;
    for (const auto &visit : problem.visits()) {
        const auto location_it = location_index.find(visit.location().get());
        ASSERT_TRUE(location_it != std::end(location_index));

        visits.emplace_back(location_it->second,
                            visit.datetime().time_of_day() - TIME_WINDOW,
                            visit.datetime().time_of_day() + TIME_WINDOW,
                            visit.duration());
    }

    // calculate distance matrix
    std::vector<int64> prototype(locations.size(), 0);
    std::vector<std::vector<int64> > distances(locations.size(), prototype);
    prototype.clear();

    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig("/home/pmateusz/dev/cordia/data/scotland-latest.osrm");
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    const osrm::OSRM engine{config};

    const auto get_distance = [&engine](rows::Location source,
                                        rows::Location destination) -> int64 {
        osrm::RouteParameters params;
        params.coordinates.emplace_back(source.longitude(), source.latitude());
        params.coordinates.emplace_back(destination.longitude(), destination.latitude());

        osrm::json::Object result;
        engine.Route(params, result);
        auto &routes = result.values["routes"].get<osrm::json::Array>();
        auto &route = routes.values.at(0).get<osrm::json::Object>();
        return static_cast<int64>(std::ceil(route.values["duration"].get<osrm::json::Number>().value));
    };

    const auto locations_size = locations.size();
    for (auto from_index = 0; from_index < locations_size; ++from_index) {
        for (auto to_index = 0; to_index < locations_size; ++to_index) {
            distances[from_index][to_index] = get_distance(locations[from_index], locations[to_index]);
        }
    }

    // initialize breaks
    std::vector<std::vector<Break> > breaks;
    for (const auto &carer_pair : problem.carers()) {
        DCHECK(carer_pair.second.empty() || carer_pair.second.size() == 1);

        std::vector<Break> local_breaks;
        for (const auto &local_break : carer_pair.second.front().Breaks()) {
            local_breaks.emplace_back(local_break.begin().time_of_day(), local_break.duration());
        }
        breaks.emplace_back(std::move(local_breaks));
    }

    const Environment data{visits, breaks, distances};

    operations_research::RoutingModel model(static_cast<int>(data.Visits.size() + 1),
                                            static_cast<int>(data.Breaks.size()),
                                            data.Depot);

    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(&data, &Environment::distance));

    static const auto FIX_CUMULATIVE_TO_ZERO = true;
    static const auto MAX_TIME_SLACK = boost::posix_time::hours(24).total_seconds();
    static const auto CAPACITY = boost::posix_time::hours(24).total_seconds();
    model.AddDimension(NewPermanentCallback(&data, &Environment::service_plus_distance),
                       MAX_TIME_SLACK,
                       CAPACITY,
                       FIX_CUMULATIVE_TO_ZERO,
                       Environment::TIME_DIM);

    auto time_dimension = model.GetMutableDimension(Environment::TIME_DIM);

    for (auto visit_node = data.Depot + 1; visit_node < model.nodes(); ++visit_node) {
        const auto &visit = data.NodeToVisit(visit_node);
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
        for (const auto &break_config : data.Breaks[vehicle]) {
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
    parameters.set_time_limit_ms(boost::posix_time::minutes(15).total_milliseconds());

    static const auto USE_ADVANCED_SEARCH = true;
    parameters.set_use_light_propagation(USE_ADVANCED_SEARCH);
    parameters.mutable_local_search_operators()->set_use_path_lns(USE_ADVANCED_SEARCH);
    parameters.mutable_local_search_operators()->set_use_inactive_lns(USE_ADVANCED_SEARCH);

    model.CloseModelWithParameters(parameters);

    const operations_research::Assignment *assignment = model.SolveWithParameters(parameters);
    ASSERT_FALSE(nullptr == assignment);
}