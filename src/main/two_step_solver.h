#ifndef ROWS_TWO_STEP_SOLVER_H
#define ROWS_TWO_STEP_SOLVER_H

#include <memory>
#include <atomic>

#include "solver_wrapper.h"
#include "solution_repository.h"

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

        std::shared_ptr<rows::SolutionRepository> solution_repository();

    private:
        int64 dropped_visit_penalty_;
        std::shared_ptr<rows::SolutionRepository> solution_repository_;
    };
}


#endif //ROWS_TWO_STEP_SOLVER_H
