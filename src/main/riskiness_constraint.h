#ifndef ROWS_RISKINESS_CONSTRAINT_H
#define ROWS_RISKINESS_CONSTRAINT_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "duration_sample.h"
#include "delay_constraint.h"

namespace rows {

    class RiskinessConstraint : public DelayConstraint {
    public:
        RiskinessConstraint(operations_research::IntVar *riskiness_index,
                            const operations_research::RoutingDimension *dimension,
                            std::shared_ptr<const DurationSample> duration_sample);

        void Post() override;

    protected:
        void PostNodeConstraints(int64 node) override;

    private:
        int64 MaxDelay(int64 index) const;

        int64 MeanDelay(int64 index) const;

        int64 GetEssentialRiskiness(int64 index) const;

        operations_research::IntVar *riskiness_index_;
    };
}


#endif //ROWS_RISKINESS_CONSTRAINT_H
