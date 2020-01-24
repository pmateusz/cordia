#include "delay_riskiness_constraint.h"

rows::DelayRiskinessConstraint::DelayRiskinessConstraint(operations_research::IntVar *riskiness_index, std::unique_ptr<DelayTracker> delay_tracker)
        : DelayConstraint(std::move(delay_tracker)),
          riskiness_index_{riskiness_index} {
}

void rows::DelayRiskinessConstraint::Post() {
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
                                                                 "RiskinessPropagateAllPaths");
    all_paths_completed_->WhenBound(all_paths_completed_demon);
}

void rows::DelayRiskinessConstraint::PostNodeConstraints(int64 node) {
    const int64 essential_riskiness = GetEssentialRiskiness(node);
    if (essential_riskiness > riskiness_index_->Min()) {
        solver()->AddConstraint(solver()->MakeGreaterOrEqual(riskiness_index_, essential_riskiness));
    }
}

int64 rows::DelayRiskinessConstraint::GetEssentialRiskiness(int64 index) const {
    std::vector<int64> delays = Delay(index);
    std::sort(std::begin(delays), std::end(delays));

    // if last element is negative then index is zero
    const auto num_delays = delays.size();
    int64 delay_pos = num_delays - 1;
    if (delays.at(delay_pos) <= 0) {
        return 0;
    }

//    if (delays.at(0) >= 0) {
//        return kint64max;
//    }

    // compute total delay
    int64 total_delay = 0;
    for (; delay_pos >= 0 && delays.at(delay_pos) >= 0; --delay_pos) {
        total_delay += delays.at(delay_pos);
    }
    CHECK_GT(total_delay, 0);
//    CHECK_GT(delay_pos, 0);

//    if (delays.at(0) >= 0) {
    if (delay_pos == -1) {
        return total_delay;
        return kint64max;
    }

    // return when not possible to increase the riskiness index
    if ((delay_pos + 1) * riskiness_index_->Min() >= total_delay) {
        return riskiness_index_->Min();
    }

    // find minimum traffic index that compensates the total delay
    int64 delay_budget = 0;
    for (; delay_pos > 0 && delay_budget + (delay_pos + 1) * delays.at(delay_pos) + total_delay > 0; --delay_pos) {
        delay_budget += delays.at(delay_pos);
    }

    int64 delay_balance = delay_budget + (delay_pos + 1) * delays.at(delay_pos) + total_delay;
    if (delay_balance < 0) {
        int64 riskiness_index = std::min(0l, delays.at(delay_pos + 1));
        CHECK_LE(riskiness_index, 0);

        int64 remaining_balance = total_delay + delay_budget + (delay_pos + 1) * riskiness_index;
        CHECK_GE(remaining_balance, 0);

        riskiness_index -= std::ceil(static_cast<double>(remaining_balance) / static_cast<double>(delay_pos + 1));
        CHECK_LE(riskiness_index * (delay_pos + 1) + delay_budget + total_delay, 0);

        return -riskiness_index;
    } else if (delay_balance > 0) {
        CHECK_EQ(delay_pos, 0);
        return delay_balance;
        return kint64max;
    }

    return delays.at(delay_pos);
}
