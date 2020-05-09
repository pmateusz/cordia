#ifndef ROWS_THIRD_STEP_SOLVER_WITH_REDUCTION_H
#define ROWS_THIRD_STEP_SOLVER_WITH_REDUCTION_H

#include "metaheuristic_solver.h"

#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class ThirdStepReductionSolver : public MetaheuristicSolver {
    public:
        ThirdStepReductionSolver(const ProblemData &problem_data,
                                 const operations_research::RoutingSearchParameters &search_parameters,
                                 boost::posix_time::time_duration visit_time_window,
                                 boost::posix_time::time_duration break_time_window,
                                 boost::posix_time::time_duration begin_end_work_day_adjustment,
                                 boost::posix_time::time_duration no_progress_time_limit,
                                 int64 max_dropped_visits);

    protected:
        void BeforeCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) override;
    };
}

#endif //ROWS_THIRD_STEP_SOLVER_WITH_REDUCTION_H
