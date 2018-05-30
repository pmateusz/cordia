#ifndef ROWS_SOLUTION_LOG_MONITOR_H
#define ROWS_SOLUTION_LOG_MONITOR_H

#include <ortools/constraint_solver/constraint_solveri.h>
#include <ortools/constraint_solver/routing.h>

#include <vector>

#include <boost/circular_buffer.hpp>

namespace rows {

    class SolutionLogMonitor : public operations_research::SearchLimit {
    public:
        explicit SolutionLogMonitor(operations_research::RoutingModel const *model);

        bool Check() override;

        bool AtSolution() override;

        void Init() override;

        void Copy(const operations_research::SearchLimit *limit) override;

        operations_research::SearchLimit *MakeClone() const override;

    private:
        operations_research::RoutingModel const *model_;

        int min_dropped_visits_;
        int cut_off_threshold_;
        boost::circular_buffer<int> dropped_visits_buffer_;
        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > min_dropped_routes_;

        bool stop_search_;
    };
}


#endif //ROWS_SOLUTION_LOG_MONITOR_H
