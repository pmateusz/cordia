#include "constraint_operations.h"

rows::ConstraintOperations::ConstraintOperations(rows::SolverWrapper &solver_wrapper,
                                                 operations_research::RoutingModel &routing_model)
        : solver_wrapper_{solver_wrapper},
          model_{routing_model},
          time_dim_{routing_model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION)} {}

void rows::ConstraintOperations::FirstVehicleNumberIsSmaller(int64 first_node,
                                                             int64 second_node) {
    model_.solver()->AddConstraint(
            model_.solver()->MakeLess(model_.VehicleVar(first_node),
                                      model_.solver()->MakeMax(model_.VehicleVar(second_node),
                                                               model_.solver()->MakeIntConst(1))));
}

void rows::ConstraintOperations::FirstVisitIsActiveIfSecondIs(int64 first_node,
                                                              int64 second_node) {
    model_.solver()->AddConstraint(model_.solver()->MakeLessOrEqual(model_.ActiveVar(second_node),
                                                                    model_.ActiveVar(first_node)));
}

void rows::ConstraintOperations::FirstVehicleArrivesNoLaterThanSecond(int64 first_node,
                                                                      int64 second_node) {
    model_.solver()->AddConstraint(model_.solver()->MakeLessOrEqual(time_dim_->CumulVar(second_node),
                                                                    time_dim_->CumulVar(first_node)));
}