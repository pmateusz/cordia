#ifndef ROWS_SOLUTION_LOG_MONITOR_H
#define ROWS_SOLUTION_LOG_MONITOR_H

#include <ortools/constraint_solver/constraint_solveri.h>
#include <ortools/constraint_solver/routing.h>

#include <memory>

#include <boost/circular_buffer.hpp>

#include "solution_repository.h"

namespace rows {

    class SolutionLogMonitor : public operations_research::SearchLimit {
    public:
        SolutionLogMonitor(operations_research::RoutingIndexManager const *index_manager,
                           operations_research::RoutingModel const *model,
                           std::shared_ptr<rows::SolutionRepository> solution_repository);

        bool Check() override;

        void EnterSearch() override;

        bool AtSolution() override;

        void Init() override;

        void Copy(const operations_research::SearchLimit *limit) override;

        operations_research::SearchLimit *MakeClone() const override;

    private:
        operations_research::RoutingIndexManager const *index_manager_;
        operations_research::RoutingModel const *model_;
        std::shared_ptr<rows::SolutionRepository> solution_repository_;

        int min_dropped_visits_;
        int cut_off_threshold_;
        boost::circular_buffer<int> dropped_visits_buffer_;

        bool stop_search_;
    };
}


#endif //ROWS_SOLUTION_LOG_MONITOR_H
