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

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            const std::atomic<bool> &cancel_token) override;

        int64 ServiceTimeWithInstantTransfer(operations_research::RoutingModel::NodeIndex from,
                                             operations_research::RoutingModel::NodeIndex to);
    };
}


#endif //ROWS_INSTANT_TRANSFER_SOLVER_H
