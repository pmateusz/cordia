#include "delay_probability_constraint.h"

rows::DelayProbabilityConstraint::DelayProbabilityConstraint(operations_research::IntVar *worst_delay_probability,
                                                             std::unique_ptr<DelayTracker> delay_tracker)
        : DelayConstraint(std::move(delay_tracker)),
          worst_delay_probability_{worst_delay_probability} {}

void rows::DelayProbabilityConstraint::Post() {
    DelayConstraint::Post();

    all_paths_completed_->WhenBound(MakeAllPathsDelayedDemon("ProbabilityPropagateAllPaths"));
}

void rows::DelayProbabilityConstraint::PostNodeConstraints(int64 node) {
    const auto delay_probability = GetDelayProbability(node);
    if (delay_probability > worst_delay_probability_->Min()) {
        solver()->AddConstraint(solver()->MakeGreaterOrEqual(worst_delay_probability_, delay_probability));
    }
}
