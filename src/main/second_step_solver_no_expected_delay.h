#ifndef ROWS_SECOND_STEP_SOLVER_NO_EXPECTED_DELAY_H
#define ROWS_SECOND_STEP_SOLVER_NO_EXPECTED_DELAY_H

#include <memory>
#include <atomic>

#include "solver_wrapper.h"
#include "solution_repository.h"
#include "history.h"

namespace rows {

    class SecondStepSolverNoExpectedDelay : public SolverWrapper {
    public:
        SecondStepSolverNoExpectedDelay(const ProblemData &problem_data,
                                        const History &history,
                                        const operations_research::RoutingSearchParameters &search_parameters,
                                        boost::posix_time::time_duration visit_time_window,
                                        boost::posix_time::time_duration break_time_window,
                                        boost::posix_time::time_duration begin_end_work_day_adjustment,
                                        boost::posix_time::time_duration no_progress_time_limit);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token,
                            double cost_normalization_factor) override;

        std::shared_ptr<rows::SolutionRepository> solution_repository();

    private:
        const History &history_;

        boost::posix_time::time_duration no_progress_time_limit_;
        operations_research::SolutionCollector *solution_collector_;
        std::shared_ptr<rows::SolutionRepository> solution_repository_;
    };
}


#endif //ROWS_SECOND_STEP_SOLVER_NO_EXPECTED_DELAY_H
