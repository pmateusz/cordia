#include "delay_not_expected_constraint.h"


rows::DelayNotExpectedConstraint::DelayNotExpectedConstraint(std::unique_ptr<DelayTracker> delay_tracker)
        : DelayConstraint(std::move(delay_tracker)) {}

void rows::DelayNotExpectedConstraint::Post() {
    DelayConstraint::Post();

//    for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
//        std::stringstream label{"NoExpectedDelayPropagateVehicle_"};
//        label << vehicle;
//
//        completed_paths_[vehicle]->WhenBound(MakePathDelayedDemon(vehicle, label.str()));
//    }

    all_paths_completed_->WhenBound(MakeAllPathsDelayedDemon("NoExpectedDelayPropagateAllPaths"));
}

void rows::DelayNotExpectedConstraint::PostNodeConstraints(int64 node) {
    const auto mean_delay = GetMeanDelay(node);
    if (mean_delay > 0) {
//        std::stringstream msg;
//        const auto &node_delays = Delay(node);
//        const std::size_t num_delays = node_delays.size();
//        for (std::size_t pos = 0; pos < num_delays; ++pos) {
//            msg << pos + 1 << " " << node_delays[pos] << std::endl;
//        }
//        LOG(INFO) << msg.str();

        solver()->Fail();
    }
}
