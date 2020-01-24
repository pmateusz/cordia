#ifndef ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H
#define ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H

#include "delay_constraint.h"

namespace rows {

    class DelayNotExpectedConstraint : public DelayConstraint {
    public:
        DelayNotExpectedConstraint(std::unique_ptr<DelayTracker> delay_tracker);

        void Post() override;

    protected:
        void PostNodeConstraints(int64 node) override;

        int64 GetExpectedDelay(int64 node) const;
    };
}


#endif //ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H
