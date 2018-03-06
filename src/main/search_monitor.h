#ifndef ROWS_SEARCH_MONITOR_H
#define ROWS_SEARCH_MONITOR_H

#include <atomic>

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

namespace rows {

    class SearchMonitor : public operations_research::SearchMonitor {
    public:
        SearchMonitor(operations_research::Solver *const solver,
                      operations_research::RoutingModel *const model,
                      const std::atomic<bool> &cancel_token);

        void EnterSearch() override;

        bool AtSolution() override;

        ~SearchMonitor() override = default;

        void BeginNextDecision(operations_research::DecisionBuilder *const builder) override;

        void RefuteDecision(operations_research::Decision *const builder) override;

    private:
        void PeriodicCheck();

        void TopPeriodicCheck();

        double Cost() const;

        boost::posix_time::time_duration WallTime() const;

        int DroppedVisits() const;

        operations_research::RoutingModel *const model_;
        const std::atomic<bool> &cancel_token_;
        bool crossed_{false};
    };
}


#endif //ROWS_SEARCH_MONITOR_H
