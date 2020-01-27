#ifndef ROWS_DELAY_CONSTRAINT_H
#define ROWS_DELAY_CONSTRAINT_H

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/constraint_solveri.h>

#include "duration_sample.h"
#include "delay_tracker.h"

namespace rows {

    class DelayConstraint : public operations_research::Constraint {
    public:
        DelayConstraint(std::unique_ptr<DelayTracker> delay_tracker);

        void Post() override;

        void InitialPropagate() override;

        void PropagatePath(int vehicle);

        void PropagateAllPaths();

    protected:
        operations_research::Demon *MakeAllPathsDelayedDemon(const std::string &demon_name);

        operations_research::Demon *MakePathDelayedDemon(int vehicle, const std::string &demon_name);

        inline int64 GetMeanDelay(int64 node) const { return delay_tracker_->GetMeanDelay(node); }

        inline int64 GetDelayProbability(int64 node) const { return delay_tracker_->GetDelayProbability(node); }

        virtual void PostNodeConstraints(int64 node) = 0;

        inline const operations_research::RoutingModel *model() const { return delay_tracker_->model(); }

        inline const std::vector<int64> &Delay(int64 node) const { return delay_tracker_->Delay(node); }

        std::vector<operations_research::IntVar *> completed_paths_;
        operations_research::IntVar *all_paths_completed_;

    private:
        void PostPathConstraints(int vehicle);

        std::unique_ptr<DelayTracker> delay_tracker_;
    };
}


#endif //ROWS_DELAY_CONSTRAINT_H
