#include "delay_not_expected_constraint.h"


rows::DelayNotExpectedConstraint::DelayNotExpectedConstraint(std::unique_ptr<DelayTracker> delay_tracker)
        : DelayConstraint(std::move(delay_tracker)) {}

void rows::DelayNotExpectedConstraint::Post() {
    DelayConstraint::Post();

    for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
        auto vehicle_demon = MakeDelayedConstraintDemon1(
                solver(), static_cast<DelayConstraint *>(this), &DelayConstraint::PropagatePath,
                "NoExpectedDelayPropagateVehicle", vehicle);

        completed_paths_[vehicle]->WhenBound(vehicle_demon);
    }

    auto all_paths_completed_demon = MakeDelayedConstraintDemon0(solver(),
                                                                 static_cast<DelayConstraint *>(this),
                                                                 &DelayConstraint::PropagateAllPaths,
                                                                 "NoExpectedDelayPropagateAllPaths");
    all_paths_completed_->WhenBound(all_paths_completed_demon);
}

void rows::DelayNotExpectedConstraint::PostNodeConstraints(int64 node) {
    const auto mean_delay = GetMeanDelay(node);
    if (mean_delay > 0) {
        solver()->Fail();
    }
}
