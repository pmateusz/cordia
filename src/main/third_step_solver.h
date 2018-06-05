#ifndef ROWS_THIRD_STEP_SOLVER_H
#define ROWS_THIRD_STEP_SOLVER_H

#include "solver_wrapper.h"

#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class ThirdStepSolver : public SolverWrapper {
    public:
        ThirdStepSolver(const Problem &problem,
                        osrm::EngineConfig &config,
                        const operations_research::RoutingSearchParameters &search_parameters,
                        boost::posix_time::time_duration visit_time_window,
                        boost::posix_time::time_duration break_time_window,
                        boost::posix_time::time_duration begin_end_work_day_adjustment,
                        boost::posix_time::time_duration no_progress_time_limit,
                        int64 dropped_visit_penalty,
                        int64 max_dropped_visits);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token) override;

        // TODO: compare schedules with humans
        // TODO: increase cost of using carers (number of carers, cost of carers, travel time)

    private:
        boost::posix_time::time_duration no_progress_time_limit_;
        int64 dropped_visit_penalty_;
        int64 max_dropped_visits_;
    };
}


#endif //ROWS_THIRD_STEP_SOLVER_H
