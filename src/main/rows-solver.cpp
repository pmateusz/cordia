#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <memory>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/range/irange.hpp>

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
#include "solver_wrapper.h"
#include "gexf_writer.h"


static const int STATUS_ERROR = 1;
static const int STATUS_OK = 1;

DEFINE_string(problem_file, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem_file, &util::ValidateFilePath);

DEFINE_string(solution_file, "", "a file path to the solution file");
DEFINE_validator(solution_file, &util::TryValidateFilePath);

DEFINE_string(map_file, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(map_file, &util::ValidateFilePath);

rows::Problem ReduceToSingleDay(const rows::Problem &problem, const boost::filesystem::path &problem_file) {
    std::set<boost::posix_time::ptime::date_type> days;
    for (const auto &visit : problem.visits()) {
        days.insert(visit.datetime().date());
    }
    boost::posix_time::ptime::date_type day_to_use = *std::min_element(std::begin(days), std::end(days));

    if (days.size() > 1) {
        LOG(WARNING) << boost::format("Problem '%1%' contains records from several days. " \
                                              "The computed solution will be reduced to a single day: '%2%'")
                        % problem_file
                        % day_to_use;
    }

    std::vector<rows::CalendarVisit> visits_to_use;
    for (const auto &visit : problem.visits()) {
        if (visit.datetime().date() == day_to_use) {
            visits_to_use.push_back(visit);
        }
    }

    std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > carers_to_use;
    for (const auto &carer_diaries : problem.carers()) {
        for (const auto &diary : carer_diaries.second) {
            if (diary.date() == day_to_use) {
                carers_to_use.emplace_back(carer_diaries.first, std::vector<rows::Diary>{diary});
            }
        }
    }

// code commented out solves an esier problem reduced to first 50 visits
// std::vector<rows::Visit> reduced_visits;
// std::copy(std::begin(visits_to_use), std::begin(visits_to_use) + 50, std::back_inserter(reduced_visits));

    return {visits_to_use, carers_to_use};
}


rows::Problem RemoveCancelledVisits(const rows::Problem &problem, const rows::Solution &solution) {
    std::vector<rows::CalendarVisit> visits_to_use;

    std::unordered_map<rows::ServiceUser, std::vector<rows::ScheduledVisit> > cancelled_visits;
    for (const auto &scheduled_visit : solution.visits()) {
        if (scheduled_visit.type() != rows::ScheduledVisit::VisitType::CANCELLED) {
            continue;
        }

        const auto &calendar_visit = scheduled_visit.calendar_visit();
        if (!calendar_visit) {
            continue;
        }

        const auto &service_user = calendar_visit.get().service_user();
        auto bucket_pair = cancelled_visits.find(service_user);
        if (bucket_pair == std::end(cancelled_visits)) {
            cancelled_visits.insert(std::make_pair(service_user, std::vector<rows::ScheduledVisit>{}));
            bucket_pair = cancelled_visits.find(service_user);
        }
        bucket_pair->second.push_back(scheduled_visit);
    }

    for (const auto &visit : problem.visits()) {
        const auto find_it = cancelled_visits.find(visit.service_user());
        if (find_it != std::end(cancelled_visits)) {
            const auto found_it = std::find_if(std::begin(find_it->second), std::end(find_it->second),
                                               [&visit](const rows::ScheduledVisit &cancelled_visit) -> bool {
                                                   const auto &local_visit = cancelled_visit.calendar_visit().get();
                                                   return visit.service_user() == local_visit.service_user()
                                                          && visit.datetime() == local_visit.datetime()
                                                          && visit.address() == local_visit.address();
                                               });

            if (found_it != std::end(find_it->second)) {
                continue;
            }
        }

        visits_to_use.push_back(visit);
    }

    return {visits_to_use, problem.carers()};
}

rows::Problem LoadReducedProblem(const std::string &problem_path) {
    boost::filesystem::path problem_file(boost::filesystem::canonical(FLAGS_problem_file));
    std::ifstream problem_stream;
    problem_stream.open(problem_file.c_str());
    if (!problem_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     STATUS_ERROR);
    }

    nlohmann::json problem_json;
    try {
        problem_stream >> problem_json;
    } catch (...) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     boost::current_exception_diagnostic_information(),
                                     STATUS_ERROR);
    }

    rows::Problem problem;
    try {
        rows::Problem::JsonLoader json_loader;
        problem = json_loader.Load(problem_json);
    } catch (const std::domain_error &ex) {
        throw util::ApplicationError(
                (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % problem_file % ex.what()).str(),
                STATUS_ERROR);
    }

    rows::Problem reduced_problem = ReduceToSingleDay(problem, problem_file);
    DCHECK(reduced_problem.IsAdmissible());
    return reduced_problem;
}

rows::Solution LoadSolution(const std::string &solution_path) {
    boost::filesystem::path solution_file(boost::filesystem::canonical(FLAGS_solution_file));
    std::ifstream solution_stream;
    solution_stream.open(solution_file.c_str());
    if (!solution_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                     STATUS_ERROR);
    }

    nlohmann::json solution_json;
    try {
        solution_stream >> solution_json;
    } catch (...) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                     boost::current_exception_diagnostic_information(),
                                     STATUS_ERROR);
    }

    try {
        rows::Solution::JsonLoader json_loader;
        return json_loader.Load(solution_json);
    } catch (const std::domain_error &ex) {
        throw util::ApplicationError(
                (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % solution_file % ex.what()).str(),
                STATUS_ERROR);
    }
}

osrm::EngineConfig CreateEngineConfig(const std::string &maps_file) {
    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig(maps_file);
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    if (!config.IsValid()) {
        throw util::ApplicationError("Invalid Open Street Map engine configuration", 1);
    }

    return config;
}

operations_research::RoutingSearchParameters CreateSearchParameters() {
    operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
    parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
    parameters.mutable_local_search_operators()->set_use_path_lns(false);
    parameters.mutable_local_search_operators()->set_use_inactive_lns(false);
    return parameters;
}

void SetupModel(operations_research::RoutingModel &model, rows::SolverWrapper &wrapper) {
    model.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(&wrapper, &rows::SolverWrapper::Distance));

    static const auto VEHICLES_CAN_START_AT_DIFFERENT_TIMES = true;
    model.AddDimension(NewPermanentCallback(&wrapper, &rows::SolverWrapper::ServiceTimePlusDistance),
                       rows::SolverWrapper::SECONDS_IN_DAY,
                       rows::SolverWrapper::SECONDS_IN_DAY,
                       VEHICLES_CAN_START_AT_DIFFERENT_TIMES,
                       rows::SolverWrapper::TIME_DIMENSION);

    operations_research::RoutingDimension *const time_dimension = model.GetMutableDimension(
            rows::SolverWrapper::TIME_DIMENSION);

    operations_research::Solver *const solver = model.solver();
    for (const auto &carer_index : wrapper.Carers()) {
        time_dimension->SetBreakIntervalsOfVehicle(wrapper.Breaks(solver, carer_index), carer_index.value());
    }

    // set visit start times
    for (auto visit_index = 1; visit_index < model.nodes(); ++visit_index) {
        const auto &visit = wrapper.CalendarVisit(operations_research::RoutingModel::NodeIndex{visit_index});
        time_dimension->CumulVar(visit_index)->SetValue(visit.datetime().time_of_day().total_seconds());
        model.AddToAssignment(time_dimension->SlackVar(visit_index));
    }

    // minimize time variables
    for (auto variable_index = 0; variable_index < model.Size(); ++variable_index) {
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(variable_index));
    }

    // minimize route duration
    for (auto carer_index = 0; carer_index < model.vehicles(); ++carer_index) {
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(model.Start(carer_index)));
        model.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(model.End(carer_index)));
    }

    // Adding penalty costs to allow skipping orders.
    const int64 kPenalty = 100000;
    const operations_research::RoutingModel::NodeIndex kFirstNodeAfterDepot(1);
    for (operations_research::RoutingModel::NodeIndex order = kFirstNodeAfterDepot; order < model.nodes(); ++order) {
        std::vector<operations_research::RoutingModel::NodeIndex> orders(1, order);
        model.AddDisjunction(orders, kPenalty);
    }
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);

    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling");
    static const bool REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\nproblem_file: %1%\nsolution_file: %2%\nmap_file: %3%")
               % FLAGS_problem_file
               % FLAGS_solution_file
               % FLAGS_map_file;

    try {
        boost::optional<rows::Solution> solution;

        auto problem_to_use = LoadReducedProblem(FLAGS_problem_file);

        if (!FLAGS_solution_file.empty()) {
            solution = LoadSolution(FLAGS_solution_file);
            problem_to_use = RemoveCancelledVisits(problem_to_use, solution.value());
        }

        auto engine_config = CreateEngineConfig(FLAGS_map_file);
        rows::SolverWrapper wrapper(problem_to_use, engine_config);
        wrapper.ComputeDistances();

        operations_research::RoutingModel routing(wrapper.NodesCount(),
                                                  wrapper.VehicleCount(),
                                                  rows::SolverWrapper::DEPOT);
        SetupModel(routing, wrapper);

        const auto parameters = CreateSearchParameters();
        const operations_research::Assignment *assignment = routing.SolveWithParameters(parameters);
        if (assignment == nullptr) {
            throw util::ApplicationError("No solution found.", STATUS_ERROR);
        }

        rows::GexfWriter solution_writer;
        solution_writer.Write("../solution.gexf", wrapper, routing, *assignment);

        wrapper.DisplayPlan(routing,
                            *assignment,
                /*use_same_vehicle_costs=*/false,
                /*max_nodes_per_group=*/0,
                /*same_vehicle_cost=*/0,
                            routing.GetDimensionOrDie(rows::SolverWrapper::TIME_DIMENSION));

        return STATUS_OK;
    } catch (util::ApplicationError &ex) {
        LOG(ERROR) << ex.msg() << std::endl << ex.diagnostic_info();
        return ex.exit_code();
    }
}