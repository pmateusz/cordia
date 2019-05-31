#ifndef ROWS_INSTANT_TRANSFER_SOLVER_H
#define ROWS_INSTANT_TRANSFER_SOLVER_H

#include <ortools/base/logging.h>
#include <ortools/constraint_solver/routing.h>

#include <osrm/engine/engine_config.hpp>

#include "solver_wrapper.h"

namespace rows {

    class InstantTransferSolver : public SolverWrapper {
    public:

        InstantTransferSolver(const rows::Problem &problem,
                              osrm::EngineConfig &config,
                              const operations_research::RoutingSearchParameters &search_parameters);

        void ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                            operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token) override;

        int64 ServiceTimeWithInstantTransfer(operations_research::RoutingNodeIndex from,
                                             operations_research::RoutingNodeIndex to);
    };
}


#endif //ROWS_INSTANT_TRANSFER_SOLVER_H
