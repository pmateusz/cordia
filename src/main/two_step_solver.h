#ifndef ROWS_TWO_STEP_SOLVER_H
#define ROWS_TWO_STEP_SOLVER_H

#include "solver_wrapper.h"

namespace rows {

    class TwoStepSolver : public SolverWrapper {
    public:
        TwoStepSolver(const Problem &problem,
                      osrm::EngineConfig &config,
                      const operations_research::RoutingSearchParameters &search_parameters);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            const std::atomic<bool> &cancel_token) override;
    };
}


#endif //ROWS_TWO_STEP_SOLVER_H
