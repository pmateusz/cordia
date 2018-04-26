#ifndef ROWS_INCREMENTAL_SOLVER_H
#define ROWS_INCREMENTAL_SOLVER_H

#include <osrm/osrm.hpp>

#include <ortools/constraint_solver/routing_parameters.pb.h>
#include <ortools/constraint_solver/routing.h>

#include "problem.h"
#include "printer.h"
#include "solver_wrapper.h"

namespace rows {

    class IncrementalSolver : public SolverWrapper {
    public:
        IncrementalSolver(const rows::Problem &problem,
                          osrm::EngineConfig &config,
                          const operations_research::RoutingSearchParameters &search_parameters,
                          boost::posix_time::time_duration break_time_window,
                          bool begin_end_work_day_adjustment_enabled);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token) override;
    };
}


#endif //ROWS_INCREMENTAL_SOLVER_H
