#ifndef ROWS_BREAK_CONSTRAINT_H
#define ROWS_BREAK_CONSTRAINT_H

#include <vector>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class BreakConstraint : public operations_research::Constraint {
    public:
        BreakConstraint(const operations_research::RoutingDimension *dimension,
                        int vehicle,
                        std::vector<operations_research::IntervalVar *> break_intervals);

        ~BreakConstraint() override = default;

        void Post() override;

        void InitialPropagate() override;

    private:
        void PathClosed();

        const operations_research::RoutingDimension *dimension_;
        const int vehicle_;
        std::vector<operations_research::IntervalVar *> break_intervals_;
        operations_research::IntVar *const status_;
    };
}


#endif //ROWS_BREAK_CONSTRAINT_H
