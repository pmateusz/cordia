#ifndef ROWS_CONSTRAINT_OPERATIONS_H
#define ROWS_CONSTRAINT_OPERATIONS_H

#include <ortools/constraint_solver/routing.h>

#include "solver_wrapper.h"

namespace rows {

    class ConstraintOperations {
    public:
        ConstraintOperations(rows::SolverWrapper &solver_wrapper, operations_research::RoutingModel &routing_model);

        void FirstVehicleNumberIsSmaller(int64 first_index, int64 second_index);

        void FirstVisitIsActiveIfSecondIs(int64 first_index, int64 second_index);

        void FirstVehicleArrivesNoLaterThanSecond(int64 first_index, int64 second_index);

    private:
        rows::SolverWrapper &solver_wrapper_;
        operations_research::RoutingModel &model_;
        operations_research::RoutingDimension *time_dim_;
    };
}


#endif //ROWS_CONSTRAINT_OPERATIONS_H
