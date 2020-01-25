#include "delay_probability_constraint.h"

rows::DelayProbabilityConstraint::DelayProbabilityConstraint(operations_research::IntVar *worst_delay_probability,
                                                             std::unique_ptr<DelayTracker> delay_tracker)
        : DelayConstraint(std::move(delay_tracker)),
          worst_delay_probability_{worst_delay_probability} {}

void rows::DelayProbabilityConstraint::Post() {
    DelayConstraint::Post();

//    for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
//        auto vehicle_demon = MakeDelayedConstraintDemon1(
//                solver(), static_cast<DelayConstraint *>(this), &DelayConstraint::PropagatePath,
//                "RiskinessPropagateVehicle", vehicle);
//
//        completed_paths_[vehicle]->WhenBound(vehicle_demon);
//    }

    auto all_paths_completed_demon = MakeDelayedConstraintDemon0(solver(),
                                                                 static_cast<DelayConstraint *>(this),
                                                                 &DelayConstraint::PropagateAllPaths,
                                                                 "ProbabilityPropagateAllPaths");
    all_paths_completed_->WhenBound(all_paths_completed_demon);
}

void rows::DelayProbabilityConstraint::PostNodeConstraints(int64 node) {
    const auto delay_probability = GetDelayProbability(node);
    if (delay_probability > worst_delay_probability_->Min()) {
        solver()->AddConstraint(solver()->MakeGreaterOrEqual(worst_delay_probability_, delay_probability));
    }
}
