#ifndef ROWS_DELAY_TRACKER_H
#define ROWS_DELAY_TRACKER_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "duration_sample.h"

namespace rows {

    class DelayTracker {
    public:
        struct TrackRecord {
            int64 index;
            int64 next;
            int64 travel_time;
            int64 break_min;
            int64 break_duration;
        };

        DelayTracker(const SolverWrapper &solver, const History &history, const operations_research::RoutingDimension *dimension);

        inline TrackRecord &Record(int64 node) { return records_.at(node); }

        inline const std::vector<int64> &Delay(int64 node) const { return delay_.at(node); }

        inline const operations_research::RoutingModel *model() const { return model_; }

        void UpdateAllPaths();

        void UpdatePath(int vehicle);

    private:
        void UpdatePathRecords(int vehicle);

        void ComputePathDelay(int vehicle);

        void PropagateNode(int64 index, std::size_t scenario);

        void PropagateNodeWithSiblings(int64 index, std::size_t scenario, std::unordered_set<int64> &siblings_updated);

        int64 GetArrivalTime(const TrackRecord &record, std::size_t scenario) const;

        const operations_research::RoutingDimension *dimension_;
        const operations_research::RoutingModel *model_;
        DurationSample duration_sample_;

        std::vector<TrackRecord> records_;
        std::vector<std::vector<int64>> start_;
        std::vector<std::vector<int64>> delay_;
    };
}


#endif //ROWS_DELAY_TRACKER_H
