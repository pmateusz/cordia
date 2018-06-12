#ifndef ROWS_MINDROPPEDVISITSSOLUTIONCOLLECTOR_H
#define ROWS_MINDROPPEDVISITSSOLUTIONCOLLECTOR_H

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

namespace rows {

    class MinDroppedVisitsSolutionCollector : public operations_research::SolutionCollector {
    public:
        MinDroppedVisitsSolutionCollector(operations_research::RoutingModel const *model);

        ~MinDroppedVisitsSolutionCollector() override = default;

        void EnterSearch() override;

        bool AtSolution() override;

        std::string DebugString() const override;

    private:
        int64 min_cost_;
        int64 min_dropped_visits_;
        operations_research::RoutingModel const *model_;
    };
}


#endif //ROWS_MINDROPPEDVISITSSOLUTIONCOLLECTOR_H
