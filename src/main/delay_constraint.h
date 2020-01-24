#ifndef ROWS_DELAY_CONSTRAINT_H
#define ROWS_DELAY_CONSTRAINT_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "duration_sample.h"

namespace rows {

    class DelayConstraint : public operations_research::Constraint {
    public:
        DelayConstraint(const operations_research::RoutingDimension *dimension, std::shared_ptr<const DurationSample> duration_sample);

        void Post() override;

        void InitialPropagate() override;

        void PropagatePath(int vehicle);

        void PropagateAllPaths();

    protected:
        virtual void PostNodeConstraints(int64 node) = 0;

        inline const operations_research::RoutingModel *model() const { return model_; }

        inline const std::vector<int64> &Delay(int64 node) const { return delay_.at(node); }

        std::vector<operations_research::IntVar *> completed_paths_;
        operations_research::IntVar *all_paths_completed_;

    private:
        struct TrackRecord {
            int64 index;
            int64 next;
            int64 travel_time;
            int64 break_min;
            int64 break_duration;
        };

        void UpdatePath(int vehicle);

        void PostPathConstraints(int vehicle);

        void ComputePathDelay(int vehicle);

        void PropagateNode(int64 index, std::size_t scenario);

        void PropagateNodeWithSiblings(int64 index, std::size_t scenario, std::unordered_set<int64> &siblings_updated);

        int64 GetArrivalTime(const TrackRecord &record, std::size_t scenario) const;

        const operations_research::RoutingModel *model_;
        const operations_research::RoutingDimension *dimension_;

        std::shared_ptr<const DurationSample> duration_sample_;

        std::vector<TrackRecord> records_;
        std::vector<std::vector<int64>> start_;
        std::vector<std::vector<int64>> delay_;
    };
}


#endif //ROWS_DELAY_CONSTRAINT_H
