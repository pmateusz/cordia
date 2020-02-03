#ifndef ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H
#define ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H

#include "delay_constraint.h"

namespace rows {

    class FailedExpectationRepository {
    public:
        void Emplace(int64 index);

        const std::unordered_set<int64> &Indices() const;

    private:
        std::unordered_set<int64> indices_;
    };

    class DelayNotExpectedConstraint : public DelayConstraint {
    public:
        DelayNotExpectedConstraint(std::unique_ptr<DelayTracker> delay_tracker,
                                   std::shared_ptr<FailedExpectationRepository> failed_expectation_repository);

        void Post() override;

    protected:
        void PostNodeConstraints(int64 node) override;

        std::shared_ptr<FailedExpectationRepository> failed_expectation_repository_;
    };
}


#endif //ROWS_DELAY_NOT_EXPECTED_CONSTRAINT_H
