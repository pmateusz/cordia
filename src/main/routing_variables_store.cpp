#include "routing_variables_store.h"

rows::RoutingVariablesStore::RoutingVariablesStore(int nodes, int vehicles)
        : nodes_{nodes},
          vehicles_{vehicles},
          node_times_{static_cast<std::size_t>(nodes), nullptr},
          node_slack_times_{static_cast<std::size_t>(nodes), nullptr},
          vehicle_break_intervals_{static_cast<std::size_t>(vehicles),
                                   std::vector<operations_research::IntervalVar *>()},
          vehicle_intervals_{static_cast<std::size_t>(vehicles), std::vector<operations_research::IntervalVar *>()} {}

void rows::RoutingVariablesStore::SetTimeVar(int node, operations_research::IntVar *var) {
    node_times_[node] = var;
}

void rows::RoutingVariablesStore::SetTimeSlackVar(int node, operations_research::IntVar *var) {
    node_slack_times_[node] = var;
}

void rows::RoutingVariablesStore::SetBreakIntervalVars(int vehicle,
                                                       std::vector<operations_research::IntervalVar *> break_intervals) {
    vehicle_break_intervals_[vehicle] = std::move(break_intervals);
}

void rows::RoutingVariablesStore::SetVehicleIntervalVars(int vehicle,
                                                         std::vector<operations_research::IntervalVar *> intervals) {
    vehicle_intervals_[vehicle] = std::move(intervals);
}

std::vector<std::vector<operations_research::IntervalVar *> > &rows::RoutingVariablesStore::vehicle_break_intervals() {
    return vehicle_break_intervals_;
}

std::vector<std::vector<operations_research::IntervalVar *> > &rows::RoutingVariablesStore::vehicle_intervals() {
    return vehicle_intervals_;
}
