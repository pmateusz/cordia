#ifndef ROWS_TWO_STEP_SOLVER_H
#define ROWS_TWO_STEP_SOLVER_H

#include "solver_wrapper.h"

namespace rows {

    class TwoStepSolver : public SolverWrapper {
    public:
        TwoStepSolver(const Problem &problem,
                      osrm::EngineConfig &config,
                      const operations_research::RoutingSearchParameters &search_parameters,
                      int64 dropped_visit_penalty);

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
    };
}


#endif //ROWS_TWO_STEP_SOLVER_H
