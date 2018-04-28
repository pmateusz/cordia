#ifndef ROWS_MULTIPLE_CARER_VISIT_CONSTRAINT_H
#define ROWS_MULTIPLE_CARER_VISIT_CONSTRAINT_H

#include <ortools/constraint_solver/constraint_solveri.h>
#include <ortools/constraint_solver/routing.h>

namespace rows {

    class MultipleCarerVisitConstraint : public operations_research::Constraint {
    public:
        MultipleCarerVisitConstraint(const operations_research::RoutingDimension *dimension,
                                     int64 first_visit,
                                     int64 second_visit);

        void Post() override;

        void InitialPropagate() override;

    private:
        void PropagateVehicle();

        void PropagateTime();

        operations_research::IntVar *first_vehicle_;
        operations_research::IntVar *first_visit_time_;

        operations_research::IntVar *second_vehicle_;
        operations_research::IntVar *second_visit_time_;
    };
}


#endif //ROWS_MULTIPLE_CARER_VISIT_CONSTRAINT_H
