#ifndef ROWS_VISIT_QUERY_H
#define ROWS_VISIT_QUERY_H

#include <memory>

#include <ortools/constraint_solver/routing.h>

#include "solver_wrapper.h"

namespace rows {

    class MultipleVisitQuery {
    public:
        MultipleVisitQuery(rows::SolverWrapper &solver_wrapper,
                           operations_research::RoutingModel &model,
                           operations_research::Assignment const *solution,
                           bool avoid_symmetry);

        bool IsRelaxed(const rows::CalendarVisit &visit) const;

        bool IsSatisfied(const rows::CalendarVisit &visit) const;

        void Print(std::shared_ptr<rows::Printer> printer) const;

        void SetAssignment(operations_research::Assignment const *solution);

    private:
        const rows::SolverWrapper &solver_wrapper_;
        const operations_research::RoutingModel &model_;
        operations_research::RoutingDimension const *time_dim_;
        operations_research::Assignment const *solution_;

        bool avoid_symmetry_;
    };
}

#endif //ROWS_VISIT_QUERY_H
