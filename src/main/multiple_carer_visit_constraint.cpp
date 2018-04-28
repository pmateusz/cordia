#include "multiple_carer_visit_constraint.h"

#include <ortools/constraint_solver/constraint_solveri.h>

rows::MultipleCarerVisitConstraint::MultipleCarerVisitConstraint(const operations_research::RoutingDimension *dimension,
                                                                 int64 first_visit,
                                                                 int64 second_visit)
        : Constraint(dimension->model()->solver()),
          first_vehicle_{dimension->model()->VehicleVar(first_visit)},
          first_visit_time_{dimension->CumulVar(first_visit)},
          second_vehicle_{dimension->model()->VehicleVar(second_visit)},
          second_visit_time_{dimension->CumulVar(second_visit)} {}

void rows::MultipleCarerVisitConstraint::Post() {
    operations_research::Demon *vehicle_demon = nullptr;
    operations_research::Demon *time_demon = nullptr;

    if (!first_vehicle_->Bound()) {
        vehicle_demon = MakeConstraintDemon0(solver(),
                                             this,
                                             &rows::MultipleCarerVisitConstraint::PropagateVehicle,
                                             "Propagate vehicle");
        first_vehicle_->WhenRange(vehicle_demon);
    }

    if (!second_vehicle_->Bound()) {
        if (!vehicle_demon) {
            vehicle_demon = MakeConstraintDemon0(solver(),
                                                 this,
                                                 &rows::MultipleCarerVisitConstraint::PropagateVehicle,
                                                 "Propagate vehicle");
        }
        second_vehicle_->WhenRange(vehicle_demon);
    }

    if (!first_visit_time_->Bound()) {
        time_demon = MakeConstraintDemon0(solver(),
                                          this,
                                          &rows::MultipleCarerVisitConstraint::PropagateTime,
                                          "Propagate time");
        first_visit_time_->WhenRange(time_demon);
    }

    if (!second_visit_time_->Bound()) {
        if (!time_demon) {
            time_demon = MakeConstraintDemon0(solver(),
                                              this,
                                              &rows::MultipleCarerVisitConstraint::PropagateTime,
                                              "Propagate time");
        }
        second_visit_time_->WhenRange(time_demon);
    }
}

void rows::MultipleCarerVisitConstraint::InitialPropagate() {
    PropagateVehicle();
    PropagateTime();
}

void rows::MultipleCarerVisitConstraint::PropagateVehicle() {
    if (first_vehicle_->Bound()) {
        const auto first_vehicle_val = first_vehicle_->Value();
        if (second_vehicle_->Bound()) {
            const auto second_vehicle_val = second_vehicle_->Value();
            if (first_vehicle_val == -1 && second_vehicle_val == -1) {
                return;
            }

            if (first_vehicle_val > -1 && first_vehicle_val < second_vehicle_val) {
                return;
            }

            LOG(INFO) << " invalid vehicles " << first_vehicle_val << " " << second_vehicle_val;
            solver()->Fail();
        } else {
            if (first_vehicle_val == -1) {
                second_vehicle_->SetValue(-1);
            } else {
                second_vehicle_->SetMin(first_vehicle_val + 1);
            }
        }
    } else if (second_vehicle_->Bound()) {
        const auto second_vehicle_val = second_vehicle_->Value();
        if (second_vehicle_val == -1) {
            first_vehicle_->SetValue(-1);
        } else {
            first_vehicle_->SetMax(second_vehicle_val - 1);
        }
    } else {
        auto first_vehicle_min_to_use = first_vehicle_->Min();
        auto first_vehicle_max_to_use = first_vehicle_->Max();
        auto second_vehicle_min_to_use = second_vehicle_->Min();
        auto second_vehicle_max_to_use = second_vehicle_->Max();

        if (first_vehicle_min_to_use < -1) {
            first_vehicle_min_to_use = -1;
        }

        if (second_vehicle_min_to_use < -1) {
            second_vehicle_min_to_use = -1;
        }

        if (first_vehicle_max_to_use >= second_vehicle_max_to_use) {
            first_vehicle_max_to_use = second_vehicle_max_to_use - 1;
        }

        if (first_vehicle_min_to_use > -1 && first_vehicle_min_to_use >= second_vehicle_min_to_use) {
            second_vehicle_min_to_use = first_vehicle_min_to_use + 1;
        }

        DCHECK_LE(first_vehicle_min_to_use, first_vehicle_max_to_use);
        DCHECK_LE(second_vehicle_min_to_use, second_vehicle_max_to_use);
        first_vehicle_->SetRange(first_vehicle_min_to_use, first_vehicle_max_to_use);
        second_vehicle_->SetRange(second_vehicle_min_to_use, second_vehicle_max_to_use);
        if (second_vehicle_->Contains(0)) {
            second_vehicle_->RemoveValue(0);
        }
    }
}

void rows::MultipleCarerVisitConstraint::PropagateTime() {
    if (first_visit_time_->Bound()) {
        const auto first_visit_time = first_visit_time_->Value();
        if (second_visit_time_->Bound()) {
            const auto second_visit_time = second_visit_time_->Value();
            if (first_visit_time != second_visit_time) {
                LOG(ERROR) << "Visit times are not equal";
                solver()->Fail();
            }
        } else {
            if (first_visit_time < second_visit_time_->Min() || second_visit_time_->Max() < first_visit_time) {
                LOG(ERROR) << first_visit_time << " is outside second visit time boundaries";
                solver()->Fail();
            } else {
                second_visit_time_->SetValue(first_visit_time);
            }
        }
    } else if (second_visit_time_->Bound()) {
        const auto second_visit_time = second_visit_time_->Value();
        if (second_visit_time < first_visit_time_->Min() || first_visit_time_->Max() < second_visit_time) {
            LOG(ERROR) << second_visit_time << " is outside first visit time boundaries";
            solver()->Fail();
        } else {
            first_visit_time_->SetValue(second_visit_time);
        }
    } else {
        const auto min_time_to_use = std::max(first_visit_time_->Min(), second_visit_time_->Min());
        const auto max_time_to_use = std::min(first_visit_time_->Max(), second_visit_time_->Max());
        if (max_time_to_use < min_time_to_use) {
            LOG(ERROR) << max_time_to_use << " is lower than " << min_time_to_use;
            solver()->Fail();
        } else {
            first_visit_time_->SetRange(min_time_to_use, max_time_to_use);
            second_visit_time_->SetRange(min_time_to_use, max_time_to_use);
        }
    }
}
