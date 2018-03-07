#ifndef ROWS_SEARCH_MONITOR_H
#define ROWS_SEARCH_MONITOR_H

#include <atomic>
#include <memory>

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

#include <printer.h>

namespace rows {

    class SearchMonitor : public operations_research::SearchMonitor {
    public:
        SearchMonitor(operations_research::Solver *const solver,
                      operations_research::RoutingModel *const model,
                      const std::shared_ptr<rows::Printer> &printer,
                      const std::atomic<bool> &cancel_token);

        bool AtSolution() override;

        ~SearchMonitor() override;

        void BeginNextDecision(operations_research::DecisionBuilder *const builder) override;

        void RefuteDecision(operations_research::Decision *const builder) override;

    private:
        void PeriodicCheck();

        void TopPeriodicCheck();

        double Cost() const;

        boost::posix_time::time_duration WallTime() const;

        std::size_t DroppedVisits() const;

        std::shared_ptr<rows::Printer> printer_;

        operations_research::RoutingModel *const model_;
        const std::atomic<bool> &cancel_token_;
        bool crossed_{false};
    };
}


#endif //ROWS_SEARCH_MONITOR_H
