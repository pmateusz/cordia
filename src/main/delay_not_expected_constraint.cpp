#include "delay_not_expected_constraint.h"

#include <utility>


rows::DelayNotExpectedConstraint::DelayNotExpectedConstraint(std::unique_ptr<DelayTracker> delay_tracker,
                                                             std::shared_ptr<FailedExpectationRepository> failed_expectation_repository)
        : DelayConstraint(std::move(delay_tracker)),
          failed_expectation_repository_{std::move(failed_expectation_repository)} {}

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
        failed_expectation_repository_->Emplace(node);
        const auto sibling_node = delay_tracker().sibling(node);
        if (sibling_node != -1) {
            failed_expectation_repository_->Emplace(sibling_node);
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

void rows::FailedExpectationRepository::Emplace(int64 index) {
    indices_.emplace(index);
}

const std::unordered_set<int64> &rows::FailedExpectationRepository::Indices() const {
    return indices_;
}