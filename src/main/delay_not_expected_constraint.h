#ifndef ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H
#define ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H

#include "delay_constraint.h"
#include "failed_index_repository.h"

namespace rows {

    class DelayNotExpectedConstraint : public DelayConstraint {
    public:
        DelayNotExpectedConstraint(std::unique_ptr<DelayTracker> delay_tracker,
                                   std::shared_ptr<FailedIndexRepository> failed_expectation_repository);

        void Post() override;

    protected:
        void PostNodeConstraints(int64 node) override;

        std::shared_ptr<FailedIndexRepository> failed_index_repository_;
    };
}


#endif //ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H
