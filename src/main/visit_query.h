#ifndef ROWS_VISIT_QUERY_H
#define ROWS_VISIT_QUERY_H

#include <memory>

#include <ortools/constraint_solver/routing.h>

#include "solver_wrapper.h"

namespace rows {

    class VisitQuery {
    public:
        VisitQuery(rows::SolverWrapper &solver_wrapper,
                   operations_research::RoutingModel &model,
                   operations_research::Assignment const *solution)
                : solver_wrapper_(solver_wrapper),
                  model_(model),
                  time_dim_(model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION)),
                  solution_(solution) {}

        bool operator()(const rows::CalendarVisit &visit) const;

        void PrintMultipleCarerVisits(std::shared_ptr<rows::Printer> printer) const;

    private:
        const rows::SolverWrapper &solver_wrapper_;
        const operations_research::RoutingModel &model_;
        operations_research::RoutingDimension const *time_dim_;
        operations_research::Assignment const *solution_;
    };
}

#endif //ROWS_VISIT_QUERY_H
