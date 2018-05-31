#ifndef ROWS_TWO_STEP_SOLVER_H
#define ROWS_TWO_STEP_SOLVER_H

#include <memory>
#include <atomic>

#include "solver_wrapper.h"
#include "solution_repository.h"

namespace rows {

    class SecondStepSolver : public SolverWrapper {
    public:
        SecondStepSolver(const Problem &problem,
                         osrm::EngineConfig &config,
                         const operations_research::RoutingSearchParameters &search_parameters,
                         boost::posix_time::time_duration visit_time_window,
                         boost::posix_time::time_duration break_time_window,
                         boost::posix_time::time_duration begin_end_work_day_adjustment,
                         boost::posix_time::time_duration no_progress_time_limit,
                         int64 dropped_visit_penalty);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token) override;

        std::shared_ptr<rows::SolutionRepository> solution_repository();

    private:
        boost::posix_time::time_duration no_progress_time_limit_;
        int64 dropped_visit_penalty_;
        std::shared_ptr<rows::SolutionRepository> solution_repository_;
    };
}


#endif //ROWS_TWO_STEP_SOLVER_H