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

    auto all_paths_completed_demon = MakeConstraintDemon0(solver(),
                                                          static_cast<DelayConstraint *>(this),
                                                          &DelayConstraint::PropagateAllPaths,
                                                          "NoExpectedDelayPropagateAllPaths");
    all_paths_completed_->WhenBound(all_paths_completed_demon);
}

void rows::DelayNotExpectedConstraint::PostNodeConstraints(int64 node) {
    const auto expected_delay = GetExpectedDelay(node);
    if (expected_delay > 0) {
        solver()->AddConstraint(solver()->MakeFalseConstraint());
    }
}

int64 rows::DelayNotExpectedConstraint::GetExpectedDelay(int64 node) const {
    const auto &delay = Delay(node);
    const int64 total_delay = std::accumulate(std::cbegin(delay), std::cend(delay), 0l);
    return total_delay / static_cast<int64>(delay.size());
}
