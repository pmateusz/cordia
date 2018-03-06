#ifndef ROWS_SEARCH_MONITOR_H
#define ROWS_SEARCH_MONITOR_H

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

namespace rows {

    class SearchMonitor : public operations_research::SearchMonitor {
    public:
        SearchMonitor(operations_research::Solver *const solver, operations_research::RoutingModel *const model);

        void EnterSearch() override;

        bool AtSolution() override;

        ~SearchMonitor() override = default;

    private:
        double Cost() const;

        boost::posix_time::time_duration WallTime() const;

        int DroppedVisits() const;

        operations_research::RoutingModel *const model_;
    };
}


#endif //ROWS_SEARCH_MONITOR_H
