#include "delay_not_expected_constraint.h"


rows::DelayNotExpectedConstraint::DelayNotExpectedConstraint(std::unique_ptr<DelayTracker> delay_tracker)
        : DelayConstraint(std::move(delay_tracker)) {}

void rows::DelayNotExpectedConstraint::Post() {
    DelayConstraint::Post();

    for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
        std::stringstream label{"NoExpectedDelayPropagateVehicle_"};
        label << vehicle;

        completed_paths_[vehicle]->WhenBound(MakePathDelayedDemon(vehicle, label.str()));
    }

    all_paths_completed_->WhenBound(MakeAllPathsDelayedDemon("NoExpectedDelayPropagateAllPaths"));
}

void rows::DelayNotExpectedConstraint::PostNodeConstraints(int64 node) {
    const auto mean_delay = GetMeanDelay(node);
    if (mean_delay > 0) {
        solver()->Fail();
    }
}
