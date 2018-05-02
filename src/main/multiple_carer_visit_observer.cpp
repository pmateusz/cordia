#include "multiple_carer_visit_observer.h"

rows::MultipleCarerVisitObserver::MultipleCarerVisitObserver(operations_research::Solver *const solver)
        : Constraint(solver) {}

void rows::MultipleCarerVisitObserver::Post() {
    operations_research::Demon *demon = solver()->MakeConstraintInitialPropagateCallback(this);
    
}

void rows::MultipleCarerVisitObserver::InitialPropagate() {

}
