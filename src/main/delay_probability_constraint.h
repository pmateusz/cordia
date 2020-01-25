#ifndef ROWS_DELAY_PROBABILITY_CONSTRAINT_H
#define ROWS_DELAY_PROBABILITY_CONSTRAINT_H

#include "delay_constraint.h"

namespace rows {

    class DelayProbabilityConstraint : public DelayConstraint {
    public:
        DelayProbabilityConstraint(operations_research::IntVar *worst_delay_probability, std::unique_ptr<DelayTracker> delay_tracker);

        void Post() override;

    protected:
        void PostNodeConstraints(int64 node) override;

    private:
        operations_research::IntVar *worst_delay_probability_;
    };
}


#endif //ROWS_DELAY_PROBABILITY_CONSTRAINT_H
