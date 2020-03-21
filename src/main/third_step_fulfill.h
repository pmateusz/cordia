#ifndef ROWS_THIRD_STEP_FULLFILL_H
#define ROWS_THIRD_STEP_FULLFILL_H

#include "metaheuristic_solver.h"

#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class ThirdStepFulfillSolver : public MetaheuristicSolver {
    public:
        ThirdStepFulfillSolver(const ProblemData &problem_data,
                               const operations_research::RoutingSearchParameters &search_parameters,
                               boost::posix_time::time_duration visit_time_window,
                               boost::posix_time::time_duration break_time_window,
                               boost::posix_time::time_duration begin_end_work_day_adjustment,
                               boost::posix_time::time_duration no_progress_time_limit,
                               int64 dropped_visit_penalty,
                               int64 max_dropped_visits,
                               std::vector<RouteValidatorBase::Metrics> vehicle_metrics);

    protected:
        void BeforeCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer) override;

    private:
        std::vector<RouteValidatorBase::Metrics> vehicle_metrics_;
    };
}


#endif //ROWS_THIRD_STEP_FULLFILL_H
