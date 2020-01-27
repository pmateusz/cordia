#include "delay_constraint.h"

#include <chrono>

// TODO: test the less rigorous definition of the index
// TODO: remove customers how are not satisfied in the expected sense

rows::DelayConstraint::DelayConstraint(std::unique_ptr<DelayTracker> delay_tracker)
        : Constraint(delay_tracker->model()->solver()),
          delay_tracker_{std::move(delay_tracker)},
          completed_paths_{},
          all_paths_completed_{nullptr} {
    completed_paths_.resize(model()->vehicles());
}

void rows::DelayConstraint::Post() {
    for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
        completed_paths_[vehicle] = solver()->MakeBoolVar();
        solver()->AddConstraint(solver()->MakePathConnected(model()->Nexts(),
                                                            {model()->Start(vehicle)},
                                                            {model()->End(vehicle)},
                                                            {completed_paths_[vehicle]}));
    }

    all_paths_completed_ = solver()->MakeIsEqualCstVar(solver()->MakeSum(completed_paths_), completed_paths_.size());
}

void rows::DelayConstraint::InitialPropagate() {
    auto has_incomplete_paths = false;
    for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
        if (!completed_paths_[vehicle]->Bound()) {
            has_incomplete_paths = true;
            break;
        }
    }

    if (has_incomplete_paths) {
        for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
            if (completed_paths_[vehicle]->Bound()) {
                PropagatePath(vehicle);
            }
        }
    } else {
        PropagateAllPaths();
    }
}

void rows::DelayConstraint::PropagatePath(int vehicle) {
    if (completed_paths_[vehicle]->Max() == 0) {
        return;
    }

    delay_tracker_->UpdatePath(vehicle);

    PostPathConstraints(vehicle);
}

void rows::DelayConstraint::PropagateAllPaths() {
    if (all_paths_completed_->Min() == 0) {
        return;
    }

    // TODO: remove time profiling
//    const auto start = std::chrono::high_resolution_clock::now();

    delay_tracker_->UpdateAllPaths();

    for (int vehicle = 0; vehicle < model()->vehicles(); ++vehicle) {
        PostPathConstraints(vehicle);
    }

//    const auto end = std::chrono::high_resolution_clock::now();
//    LOG(INFO) << "FullPropagation in  " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms: ("
//              << model_->CostVar()->Min() << ", " << model_->CostVar()->Max() << ")";
}

void rows::DelayConstraint::PostPathConstraints(int vehicle) {
    int64 current_index = delay_tracker_->Record(model()->Start(vehicle)).next;
    while (!model()->IsEnd(current_index)) {
        PostNodeConstraints(current_index);
        current_index = delay_tracker_->Record(current_index).next;
    }
}

operations_research::Demon *rows::DelayConstraint::MakeAllPathsDelayedDemon(const std::string &demon_name) {
    return MakeDelayedConstraintDemon0(solver(), this, &DelayConstraint::PropagateAllPaths, demon_name);
}

operations_research::Demon *rows::DelayConstraint::MakePathDelayedDemon(int vehicle, const std::string &demon_name) {
    return MakeDelayedConstraintDemon1(solver(), this, &DelayConstraint::PropagatePath, demon_name, vehicle);
}
