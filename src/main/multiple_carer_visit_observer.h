#ifndef ROWS_MULTIPLE_CARER_VISIT_OBSERVER_H
#define ROWS_MULTIPLE_CARER_VISIT_OBSERVER_H

#include <ortools/constraint_solver/constraint_solver.h>

namespace rows {

    class MultipleCarerVisitObserver : public operations_research::Constraint {
    public:
        MultipleCarerVisitObserver(operations_research::Solver *const solver);

        void Post() override;

        void InitialPropagate() override;
    };
}


#endif //ROWS_MULTIPLE_CARER_VISIT_OBSERVER_H
