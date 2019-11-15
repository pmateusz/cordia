#ifndef ROWS_MULTI_CARER_SOLVER_H
#define ROWS_MULTI_CARER_SOLVER_H

#include <unordered_map>
#include <string>

#include <ortools/base/logging.h>
#include <ortools/constraint_solver/routing.h>

#include <osrm/engine/engine_config.hpp>

#include "service_user.h"
#include "solver_wrapper.h"
#include "solution_repository.h"

namespace rows {

    class MultiCarerSolver : public SolverWrapper {
    public:
        MultiCarerSolver(const rows::ProblemData &problem_data,
                         const operations_research::RoutingSearchParameters &search_parameters,
                         boost::posix_time::time_duration visit_time_window,
                         boost::posix_time::time_duration break_time_window,
                         boost::posix_time::time_duration begin_end_work_day_adjustment,
                         boost::posix_time::time_duration no_progress_time_limit);

        void ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                            operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token) override;

        operations_research::Assignment *GetBestSolution() const;

    private:
        boost::posix_time::time_duration no_progress_time_limit_;
        operations_research::SolutionCollector *solution_collector_;
    };
}


#endif //ROWS_MULTI_CARER_SOLVER_H
