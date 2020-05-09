#ifndef ROWS_DELAY_RISKINESS_CONSTRAINT_H
#define ROWS_DELAY_RISKINESS_CONSTRAINT_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "delay_constraint.h"
#include "delay_tracker.h"
#include "failed_index_repository.h"

namespace rows {

    class DelayRiskinessConstraint : public DelayConstraint {
    public:
        DelayRiskinessConstraint(operations_research::IntVar *riskiness_index,
                                 std::unique_ptr<DelayTracker> delay_tracker,
                                 std::shared_ptr<FailedIndexRepository> failed_index_repository);

        void Post() override;

    protected:
        void PostNodeConstraints(int64 node) override;

    private:
        int64 GetEssentialRiskiness(int64 index) const;

        operations_research::IntVar *riskiness_index_;
        std::shared_ptr<FailedIndexRepository> failed_index_repository_;
    };
}


#endif //ROWS_DELAY_RISKINESS_CONSTRAINT_H
