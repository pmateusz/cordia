#ifndef ROWS_ROUTING_VARIABLES_STORE_H
#define ROWS_ROUTING_VARIABLES_STORE_H

#include <vector>

#include <ortools/constraint_solver/constraint_solver.h>


namespace rows {

    class RoutingVariablesStore {
    public:
        RoutingVariablesStore(int nodes, int vehicles);

        void SetTimeVar(int node, operations_research::IntVar *var);

        void SetTimeSlackVar(int node, operations_research::IntVar *var);

        void SetBreakIntervalVars(int vehicle, std::vector<operations_research::IntervalVar *> break_intervals);

        std::vector<std::vector<operations_research::IntervalVar *> > &vehicle_break_intervals();

    private:
        int nodes_;
        int vehicles_;

        std::vector<operations_research::IntVar *> node_times_;
        std::vector<operations_research::IntVar *> node_slack_times_;
        std::vector<std::vector<operations_research::IntervalVar *> > vehicle_break_intervals_;
    };
}


#endif //ROWS_ROUTING_VARIABLES_STORE_H
