#ifndef ROWS_RISKINESS_CONSTRAINT_H
#define ROWS_RISKINESS_CONSTRAINT_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class RiskinessConstraint : public operations_research::Constraint {
    public:
        RiskinessConstraint(operations_research::IntVar *riskiness_index, const operations_research::RoutingDimension *dimension);

        void Post() override;

        void InitialPropagate() override;

    private:
        void PropagateVehicle(int vehicle);

        operations_research::IntVar *riskiness_index_;
        const operations_research::RoutingModel *model_;
        const operations_research::RoutingDimension *dimension_;


        std::vector<operations_research::IntVar *> completed_paths_;
        std::vector<operations_research::Demon *> vehicle_demons_;
    };
}


#endif //ROWS_RISKINESS_CONSTRAINT_H
