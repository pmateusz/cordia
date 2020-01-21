#ifndef ROWS_BREAK_CONSTRAINT_H
#define ROWS_BREAK_CONSTRAINT_H

#include <vector>
#include <memory>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "real_problem_data.h"

namespace rows {

    class BreakConstraint : public operations_research::Constraint {
    public:
        BreakConstraint(const operations_research::RoutingDimension *dimension,
                        const operations_research::RoutingIndexManager *index_manager,
                        int vehicle,
                        std::vector<operations_research::IntervalVar *> break_intervals,
                        RealProblemData &problem_data);

        ~BreakConstraint() override = default;

        void Post() override;

        void InitialPropagate() override;

    private:
        void OnPathClosed();

        const operations_research::RoutingDimension *dimension_;
        const operations_research::RoutingIndexManager *index_manager_;
        const int vehicle_;
        std::vector<operations_research::IntervalVar *> break_intervals_;
        operations_research::IntVar *const status_;
        RealProblemData &problem_data_;
    };
}

std::ostream &operator<<(std::ostream &out, const std::vector<int64> &object);


#endif //ROWS_BREAK_CONSTRAINT_H
