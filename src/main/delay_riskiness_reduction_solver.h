#ifndef ROWS_DELAY_RISKINESS_REDUCTION_SOLVER_H
#define ROWS_DELAY_RISKINESS_REDUCTION_SOLVER_H

#include "metaheuristic_solver.h"
#include "history.h"

#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class DelayRiskinessReductionSolver : public MetaheuristicSolver {
    public:
        DelayRiskinessReductionSolver(const ProblemData &problem_data,
                                      const History &history,
                                      const operations_research::RoutingSearchParameters &search_parameters,
                                      boost::posix_time::time_duration visit_time_window,
                                      boost::posix_time::time_duration break_time_window,
                                      boost::posix_time::time_duration begin_end_work_day_adjustment,
                                      boost::posix_time::time_duration no_progress_time_limit,
                                      int64 dropped_visit_penalty,
                                      int64 max_dropped_visits);

    protected:
        void BeforeCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) override;

        void AfterCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) override;

    private:
        const History &history_;

        operations_research::IntVar *riskiness_index_;
    };
}


#endif //ROWS_DELAY_RISKINESS_REDUCTION_SOLVER_H
