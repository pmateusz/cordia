#ifndef ROWS_DELAY_PROBABILITY_REDUCTION_SOLVER_H
#define ROWS_DELAY_PROBABILITY_REDUCTION_SOLVER_H

#include "solver_wrapper.h"

#include "history.h"
#include "metaheuristic_solver.h"

#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class DelayProbabilityReductionSolver : public MetaheuristicSolver {
    public:
        DelayProbabilityReductionSolver(const ProblemData &problem_data,
                                        const History &history,
                                        const operations_research::RoutingSearchParameters &search_parameters,
                                        boost::posix_time::time_duration visit_time_window,
                                        boost::posix_time::time_duration break_time_window,
                                        boost::posix_time::time_duration begin_end_work_day_adjustment,
                                        boost::posix_time::time_duration no_progress_time_limit,
                                        int64 max_dropped_visits);

    protected:
        void BeforeCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) override;

        void AfterCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) override;

    private:
        const History &history_;

        operations_research::IntVar *delay_probability_;
    };
}

#endif //ROWS_DELAY_PROBABILITY_REDUCTION_SOLVER_H
