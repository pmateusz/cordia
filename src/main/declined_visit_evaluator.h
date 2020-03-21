#ifndef ROWS_DECLINED_VISIT_EVALUATOR_H
#define ROWS_DECLINED_VISIT_EVALUATOR_H

#include <vector>
#include <ortools/constraint_solver/constraint_solveri.h>
#include <ortools/constraint_solver/routing.h>

namespace rows {
    class ProblemData;

    class DeclinedVisitEvaluator {
    public:
        DeclinedVisitEvaluator(const ProblemData &problem_data, const operations_research::RoutingIndexManager &index_manager);

        int64 GetDroppedVisits(const operations_research::RoutingModel &model) const;

        int64 GetThreshold(const std::vector<std::vector<int64>> &routes) const;

        int64 Weight(int64 index) const;

    private:
        std::vector<int64> weights_;
    };
}


#endif //ROWS_DECLINED_VISIT_EVALUATOR_H
