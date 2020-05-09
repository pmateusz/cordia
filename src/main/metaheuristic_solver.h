#ifndef ROWS_METAHEURISTIC_SOLVER_H
#define ROWS_METAHEURISTIC_SOLVER_H

#include "solver_wrapper.h"

#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class MetaheuristicSolver : public SolverWrapper {
    public:
        MetaheuristicSolver(const ProblemData &problem_data,
                            const operations_research::RoutingSearchParameters &search_parameters,
                            boost::posix_time::time_duration visit_time_window,
                            boost::posix_time::time_duration break_time_window,
                            boost::posix_time::time_duration begin_end_work_day_adjustment,
                            boost::posix_time::time_duration no_progress_time_limit,
                            int64 max_dropped_visits_threshold);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token,
                            double cost_normalization_factor) override;

    protected:
        virtual void BeforeCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer);

        virtual void AfterCloseModel(operations_research::RoutingModel &model, const std::shared_ptr<Printer> &printer);

    private:
        boost::posix_time::time_duration no_progress_time_limit_;
        int64 max_dropped_visits_threshold_;
    };
}

#endif //ROWS_METAHEURISTIC_SOLVER_H
