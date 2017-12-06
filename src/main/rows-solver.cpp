#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/range/irange.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <nlohmann/json.hpp>

#include "util/logging.h"

#include "ortools/constraint_solver/routing.h"
#include "ortools/constraint_solver/routing_flags.h"

#include "location.h"
#include "location_container.h"
#include "carer.h"
#include "event.h"
#include "diary.h"
#include "visit.h"
#include "problem.h"


static bool ValidateProblemInstance(const char *flagname, const std::string &value) {
    boost::filesystem::path file_path(value);
    if (!boost::filesystem::exists(file_path)) {
        LOG(ERROR) << boost::format("File '%1%' does not exist") % file_path;
        return false;
    }

    if (!boost::filesystem::is_regular_file(file_path)) {
        LOG(ERROR) << boost::format("Path '%1%' does not point to a file") % file_path;
        return false;
    }

    return true;
}

DEFINE_string(problem_instance, "problem.json", "a file path to the problem instance");
DEFINE_validator(problem_instance, &ValidateProblemInstance);

class SolverWrapper {
public:
    static const operations_research::RoutingModel::NodeIndex DEPOT;

    explicit SolverWrapper(const rows::Problem &problem)
            : problem_(problem),
              location_container_() {
        for (const auto &visit : problem.visits()) {
            location_container_.Add(visit.location());
        }
    }

    int64 Distance(operations_research::RoutingModel::NodeIndex from,
                   operations_research::RoutingModel::NodeIndex to) const {
        if (from == DEPOT || to == DEPOT) {
            return 0;
        }

        return location_container_.Distance(Visit(from).location(), Visit(to).location());
    }

    int64 ServiceTimePlusDistance(operations_research::RoutingModel::NodeIndex from,
                                  operations_research::RoutingModel::NodeIndex to) const {
        if (from == DEPOT || to == DEPOT) {
            return 0;
        }

        const auto value = Visit(from).duration().total_seconds() + Distance(from, to);
        return value;
    }

    rows::Visit Visit(const operations_research::RoutingModel::NodeIndex index) const {
        DCHECK_NE(index, DEPOT);

        return problem_.visits()[index.value() - 1];
    }

    rows::Diary Diary(const operations_research::RoutingModel::NodeIndex index) const {
        const auto carer_pair = problem_.carers()[index.value()];
        DCHECK_EQ(carer_pair.second.size(), 1);
        return carer_pair.second[0];
    }

    rows::Carer Carer(const operations_research::RoutingModel::NodeIndex index) const {
        const auto &carer_pair = problem_.carers()[index.value()];
        return carer_pair.first;
    }

    std::vector<operations_research::RoutingModel::NodeIndex> Carers() const {
        std::vector<operations_research::RoutingModel::NodeIndex> result(problem_.carers().size());
        std::iota(std::begin(result), std::end(result), operations_research::RoutingModel::NodeIndex(0));
        return result;
    }

    int NodesCount() const {
        return static_cast<int>(problem_.visits().size() + 1);
    }

    int VehicleCount() const {
        return static_cast<int>(problem_.carers().size());
    }

private:
    const rows::Problem &problem_;
    rows::LocationContainer location_container_;
};

const operations_research::RoutingModel::NodeIndex SolverWrapper::DEPOT(0);

int main(int argc, char **argv) {

    const char *kTime = "Time";

    static const int STATUS_ERROR = 1;
    static const int STATUS_OK = 1;

    util::SetupLogging(argv[0]);

    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling");
    static const bool REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    boost::filesystem::path problem_file(boost::filesystem::canonical(FLAGS_problem_instance));
    LOG(INFO) << boost::format("Launched program with arguments: %1%") % problem_file;

    std::ifstream problem_stream(problem_file.c_str());
    if (!problem_stream.is_open()) {
        LOG(ERROR) << boost::format("Failed to open file: %1%") % problem_file;
        return STATUS_ERROR;
    }

    nlohmann::json json;
    try {
        problem_stream >> json;
    } catch (...) {
        LOG(ERROR) << boost::current_exception_diagnostic_information();
        return STATUS_ERROR;
    }

    rows::Problem problem;
    try {
        rows::Problem::JsonLoader json_loader;
        problem = json_loader.Load(json);
    } catch (const std::domain_error &ex) {
        LOG(ERROR) << boost::format("Failed to parse file '%1%' due to error: '%2%'") % problem_file % ex.what();
        return STATUS_ERROR;
    }

    std::set<boost::gregorian::date> days;
    for (const auto &visit : problem.visits()) {
        days.insert(visit.date());
    }
    boost::gregorian::date day_to_use = *std::min_element(std::begin(days), std::end(days));

    if (days.size() > 1) {
        LOG(WARNING) << boost::format("Problem '%1%' contains records from several days. " \
                                              "The computed solution will be reduced to a single day: '%2%'")
                        % problem_file
                        % day_to_use;
    }

    std::vector<rows::Visit> visits_to_use;
    for (const auto &visit : problem.visits()) {
        if (visit.date() == day_to_use) {
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

    rows::Problem reduced_problem(visits_to_use, carers_to_use);
    DCHECK(reduced_problem.IsAdmissible());

    SolverWrapper wrapper(reduced_problem);
    operations_research::RoutingModel routing(wrapper.NodesCount(), wrapper.VehicleCount(), SolverWrapper::DEPOT);

    routing.SetArcCostEvaluatorOfAllVehicles(NewPermanentCallback(&wrapper, &SolverWrapper::Distance));

    const int64 kHorizon = 24 * 3600;
    routing.AddDimension(NewPermanentCallback(&wrapper, &SolverWrapper::ServiceTimePlusDistance),
                         kHorizon, kHorizon, /*fix_start_cumul_to_zero=*/false, kTime);

    operations_research::RoutingDimension *const time_dimension = routing.GetMutableDimension(kTime);

    operations_research::Solver *const solver = routing.solver();
    for (const auto &carer_index : wrapper.Carers()) {
        const auto &diary = wrapper.Diary(carer_index);
        const auto &begin_time = diary.begin_time();
        const auto &end_time = diary.end_time();

        std::vector<operations_research::IntervalVar *> breaks{
                solver->MakeFixedDurationIntervalVar(
                        /*start min*/ 0,
                        /*start max*/ 0,
                        /*break duration*/ begin_time.total_seconds(),
                        /*optional*/ false,
                                      (boost::format("Carer '%1%; before working hours") % carer_index.value()).str()),
                // TODO: breaks within day are ignored
                solver->MakeFixedDurationIntervalVar(
                        /*start min*/ end_time.total_seconds(),
                        /*start max*/ end_time.total_seconds(),
                        /*break duration*/ kHorizon - end_time.total_seconds(),
                        /*optional*/ false,
                                      (boost::format("Carer '%1%; after working hours") % carer_index.value()).str())
        };

        time_dimension->SetBreakIntervalsOfVehicle(std::move(breaks), carer_index.value());
    }

    // set time windows
    for (auto order = 1; order < routing.nodes(); ++order) {
        const auto &visit = wrapper.Visit(operations_research::RoutingModel::NodeIndex(order));
        time_dimension->CumulVar(order)->SetRange(visit.time().total_seconds(),
                                                  (visit.time() + visit.duration()).total_seconds());
        routing.AddToAssignment(time_dimension->SlackVar(order));
    }

    // minimize time variables
    for (auto variable_index = 0; variable_index < routing.Size(); ++variable_index) {
        routing.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(variable_index));
    }

    // minimize route duration
    for (auto carer_index = 0; carer_index < routing.vehicles(); ++carer_index) {
        routing.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(routing.Start(carer_index)));
        routing.AddVariableMinimizedByFinalizer(time_dimension->CumulVar(routing.End(carer_index)));
    }

    // Adding penalty costs to allow skipping orders.
    const int64 kPenalty = 100000;
    const operations_research::RoutingModel::NodeIndex kFirstNodeAfterDepot(1);
    for (operations_research::RoutingModel::NodeIndex order = kFirstNodeAfterDepot; order < routing.nodes(); ++order) {
        std::vector<operations_research::RoutingModel::NodeIndex> orders(1, order);
        routing.AddDisjunction(orders, kPenalty);
    }

    operations_research::RoutingSearchParameters parameters = operations_research::BuildSearchParametersFromFlags();
    parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);
    parameters.mutable_local_search_operators()->set_use_path_lns(false);
    parameters.mutable_local_search_operators()->set_use_inactive_lns(false);

    const operations_research::Assignment *solution = routing.SolveWithParameters(parameters);
    if (solution == nullptr) {
        LOG(INFO) << "No solution found.";
        return STATUS_ERROR;
    }

    return STATUS_OK;
}