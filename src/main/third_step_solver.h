#ifndef ROWS_THIRD_STEP_SOLVER_H
#define ROWS_THIRD_STEP_SOLVER_H

#include "solver_wrapper.h"

#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class ThirdStepSolver : public SolverWrapper {
    public:
        ThirdStepSolver(const Problem &problem,
                        osrm::EngineConfig &config,
                        int64 dropped_visit_penalty,
                        int64 max_dropped_visits,
                        const operations_research::RoutingSearchParameters &search_parameters);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token) override;

        // TODO: add log search monitor and investigate if penalty should be increased
        // TODO: solve scheduling for multiple days
        // TODO: compare schedules with humans
        // TODO: increase cost of using carers (number of carers, cost of carers, travel time)
        // TODO: have a percentage value reflecting cost

    private:
        int64 dropped_visit_penalty_;
        int64 max_dropped_visits_;
    };
}


#endif //ROWS_THIRD_STEP_SOLVER_H
