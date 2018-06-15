#ifndef ROWS_BREAK_CONSTRAINT_H
#define ROWS_BREAK_CONSTRAINT_H

#include <vector>
#include <memory>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "solver_wrapper.h"

namespace rows {

    class BreakConstraint : public operations_research::Constraint {
    public:
        BreakConstraint(const operations_research::RoutingDimension *dimension,
                        int vehicle,
                        std::vector<operations_research::IntervalVar *> break_intervals,
                        SolverWrapper &solver_wrapper);

        ~BreakConstraint() override = default;

        void Post() override;

        void InitialPropagate() override;

    private:
        void OnPathClosed();

        const operations_research::RoutingDimension *dimension_;
        const int vehicle_;
        std::vector<operations_research::IntervalVar *> break_intervals_;
        operations_research::IntVar *const status_;
        SolverWrapper &solver_;
    };
}

std::ostream &operator<<(std::ostream &out, const std::vector<int64> &object);


#endif //ROWS_BREAK_CONSTRAINT_H
