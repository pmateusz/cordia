#ifndef ROWS_RISKINESS_CONSTRAINT_H
#define ROWS_RISKINESS_CONSTRAINT_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "duration_sample.h"

namespace rows {

    class RiskinessConstraint : public operations_research::Constraint {
    public:
        RiskinessConstraint(operations_research::IntVar *riskiness_index,
                            const operations_research::RoutingDimension *dimension,
                            std::shared_ptr<const DurationSample> duration_sample);

        void Post() override;

        void InitialPropagate() override;

    private:
        struct TrackRecord {
            int64 index;
            int64 next;
            int64 travel_time;
            int64 break_min;
            int64 break_duration;
        };

        void PropagateVehicle(int vehicle);

        void ResetNode(int64 index);

        void PropagateNode(int64 index, std::size_t scenario);

        void ComputeDelay(const std::vector<int64> &start_nodes);

        int64 MaxDelay(int64 index) const;

        int64 MeanDelay(int64 index) const;

        operations_research::IntVar *riskiness_index_;
        const operations_research::RoutingModel *model_;
        const operations_research::RoutingDimension *dimension_;

        std::shared_ptr<const DurationSample> duration_sample_;

        std::vector<operations_research::IntVar *> completed_paths_;
        std::vector<operations_research::Demon *> vehicle_demons_;

        std::vector<TrackRecord> records_;
        std::vector<std::vector<int64>> start_;
        std::vector<std::vector<int64>> delay_;
    };
}


#endif //ROWS_RISKINESS_CONSTRAINT_H