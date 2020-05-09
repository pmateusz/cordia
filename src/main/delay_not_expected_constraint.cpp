#include "delay_not_expected_constraint.h"

#include <utility>


rows::DelayNotExpectedConstraint::DelayNotExpectedConstraint(std::unique_ptr<DelayTracker> delay_tracker,
                                                             std::shared_ptr<FailedIndexRepository> failed_index_repository)
        : DelayConstraint(std::move(delay_tracker)),
          failed_index_repository_{std::move(failed_index_repository)} {}

void rows::DelayNotExpectedConstraint::Post() {
    DelayConstraint::Post();

//    for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
//        std::stringstream label{"NoExpectedDelayPropagateVehicle_"};
//        label << vehicle;
//        completed_paths_[vehicle]->WhenBound(MakePathDelayedDemon(vehicle, label.str()));
//    }

    all_paths_completed_->WhenBound(MakeAllPathsDelayedDemon("NoExpectedDelayPropagateAllPaths"));
}

void rows::DelayNotExpectedConstraint::PostNodeConstraints(int64 node) {
    const auto mean_delay = GetMeanDelay(node);
    if (mean_delay > 0) {
        failed_index_repository_->Emplace(node);
        const auto sibling_node = delay_tracker().sibling(node);
        if (sibling_node != -1) {
            failed_index_repository_->Emplace(sibling_node);
        }

//        std::stringstream msg;
//        const std::size_t num_delays = node_delays.size();
//        for (std::size_t pos = 0; pos < num_delays; ++pos) {
//            msg << pos + 1 << " " << node_delays[pos] << std::endl;
//        }
//        LOG(INFO) << msg.str();

        solver()->Fail();
    }
}