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

    std::vector<operations_research::IntervalVar *> Breaks(operations_research::Solver *const solver,
                                                           const operations_research::RoutingModel::NodeIndex carer) const {
        std::vector<operations_research::IntervalVar *> result;

        const auto &diary = Diary(carer);

        boost::posix_time::ptime last_end_time(diary.date());
        boost::posix_time::ptime next_day(diary.date() + boost::gregorian::date_duration(1));

        BreakType break_type = BreakType::BEFORE_WORKDAY;
        for (const auto &event : diary.events()) {

            result.push_back(CreateBreak(solver,
                                         last_end_time.time_of_day(),
                                         event.begin() - last_end_time,
                                         GetBreakLabel(carer, break_type)));

            last_end_time = event.end();
            break_type = BreakType::BREAK;
        }

        break_type = BreakType::AFTER_WORKDAY;
        result.push_back(CreateBreak(solver,
                                     last_end_time.time_of_day(),
                                     next_day - last_end_time,
                                     GetBreakLabel(carer, break_type)));

        return std::move(result);
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

    void DisplayPlan(const operations_research::RoutingModel &routing,
                     const operations_research::Assignment &plan,
                     bool use_same_vehicle_costs,
                     int64 max_nodes_per_group,
                     int64 same_vehicle_cost,
                     const operations_research::RoutingDimension &time_dimension) {
        std::stringstream out;
        out << boost::format("Cost %1% ") % plan.ObjectiveValue() << std::endl;

        std::stringstream dropped_stream;
        for (int order = 1; order < routing.nodes(); ++order) {
            if (plan.Value(routing.NextVar(order)) == order) {
                if (dropped_stream.rdbuf()->in_avail() == 0) {
                    dropped_stream << ' ' << order;
                } else {
                    dropped_stream << ',' << ' ' << order;
                }
            }
        }

        if (dropped_stream.rdbuf()->in_avail() > 0) {
            out << "Dropped orders:" << dropped_stream.str() << std::endl;
        }

        if (use_same_vehicle_costs) {
            int group_size = 0;
            int64 group_same_vehicle_cost = 0;
            std::set<int64> visited;
            const operations_research::RoutingModel::NodeIndex kFirstNodeAfterDepot(1);
            for (operations_research::RoutingModel::NodeIndex order = kFirstNodeAfterDepot;
                 order < routing.nodes(); ++order) {
                ++group_size;
                visited.insert(plan.Value(routing.VehicleVar(routing.NodeToIndex(order))));
                if (group_size == max_nodes_per_group) {
                    if (visited.size() > 1) {
                        group_same_vehicle_cost += (visited.size() - 1) * same_vehicle_cost;
                    }
                    group_size = 0;
                    visited.clear();
                }
            }
            if (visited.size() > 1) {
                group_same_vehicle_cost += (visited.size() - 1) * same_vehicle_cost;
            }
            LOG(INFO) << "Same vehicle costs: " << group_same_vehicle_cost;
        }

        for (int route_number = 0; route_number < routing.vehicles(); ++route_number) {
            int64 order = routing.Start(route_number);
            out << boost::format("Route %1%: ") % route_number;

            if (routing.IsEnd(plan.Value(routing.NextVar(order)))) {
                out << "Empty" << std::endl;
            } else {
                while (true) {
                    operations_research::IntVar *const time_var =
                            time_dimension.CumulVar(order);
                    operations_research::IntVar *const slack_var =
                            routing.IsEnd(order) ? nullptr : time_dimension.SlackVar(order);
                    if (slack_var != nullptr && plan.Contains(slack_var)) {
                        out << boost::format("%1% Time(%2%, %3%) Slack(%4%, %5%) -> ")
                               % order
                               % plan.Min(time_var) % plan.Max(time_var)
                               % plan.Min(slack_var) % plan.Max(slack_var);
                    } else {
                        out << boost::format("%1% Time(%2%, %3%) ->")
                               % order
                               % plan.Min(time_var)
                               % plan.Max(time_var);
                    }
                    if (routing.IsEnd(order)) break;
                    order = plan.Value(routing.NextVar(order));
                }
                out << std::endl;
            }
        }
        LOG(INFO) << out.str();
    }

private:
    enum class BreakType {
        BREAK, BEFORE_WORKDAY, AFTER_WORKDAY
    };

    static operations_research::IntervalVar *CreateBreak(operations_research::Solver *const solver,
                                                         boost::posix_time::time_duration start_time,
                                                         boost::posix_time::time_duration duration,
                                                         std::string label) {
        static const auto IS_OPTIONAL = false;

        return solver->MakeFixedDurationIntervalVar(
                /*start min*/ start_time.total_seconds(),
                /*start max*/ start_time.total_seconds(),
                              duration.total_seconds(),
                              IS_OPTIONAL,
                              label);
    }

    static std::string GetBreakLabel(const operations_research::RoutingModel::NodeIndex carer, BreakType break_type) {
        switch (break_type) {
            case BreakType::BEFORE_WORKDAY:
                return (boost::format("Carer '%1%' before workday") % carer).str();
            case BreakType::AFTER_WORKDAY:
                return (boost::format("Carer '%1%' after workday") % carer).str();
            case BreakType::BREAK:
                return (boost::format("Carer '%1%' break") % carer).str();
            default:
                throw std::domain_error((boost::format("Handling label '%1%' is not implemented") % carer).str());
        }
    }

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

    // FIXME
    std::vector<rows::Visit> test_visits;
    std::copy(std::cbegin(visits_to_use), std::cbegin(visits_to_use) + 10, std::back_inserter(test_visits));

    rows::Problem reduced_problem(test_visits, carers_to_use);
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
        time_dimension->SetBreakIntervalsOfVehicle(wrapper.Breaks(solver, carer_index), carer_index.value());
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
    wrapper.DisplayPlan(routing,
                        *solution,
            /*use_same_vehicle_costs=*/false,
            /*max_nodes_per_group=*/0,
            /*same_vehicle_cost=*/0,
                        routing.GetDimensionOrDie(kTime));

    return STATUS_OK;
}