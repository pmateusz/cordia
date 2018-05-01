#ifndef ROWS_INCREMENTAL_WORKER_H
#define ROWS_INCREMENTAL_WORKER_H

#include <exception>
#include <memory>
#include <iterator>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/date_time.hpp>
#include <boost/algorithm/string/join.hpp>

#include <ortools/constraint_solver/routing.h>

#include <osrm/engine/engine_config.hpp>

#include "scheduling_worker.h"
#include "printer.h"
#include "incremental_solver.h"

namespace rows {

    class IncrementalSchedulingWorker : public rows::SchedulingWorker {
    public:
        explicit IncrementalSchedulingWorker(std::shared_ptr<rows::Printer> printer);

        void Run() override;

        bool Init(rows::Problem problem,
                  osrm::EngineConfig routing_params,
                  const operations_research::RoutingSearchParameters &search_params,
                  std::string output_file);

    private:
        class ConstraintOperations {
        public:
            ConstraintOperations(rows::SolverWrapper &solver_wrapper, operations_research::RoutingModel &routing_model);

            void FirstVehicleNumberIsSmaller(int64 first_index, int64 second_index);

            void FirstVisitIsActiveIfSecondIs(int64 first_index, int64 second_index);

            void FirstVehicleArrivesNoLaterThanSecond(int64 first_index, int64 second_index);

        private:
            rows::SolverWrapper &solver_wrapper_;
            operations_research::RoutingModel &model_;
            operations_research::RoutingDimension *time_dim_;
        };

        void PrintRoutes(const std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > &routes) const;

        void PrintMultipleCarerVisits(const operations_research::Assignment &assignment,
                                      const operations_research::RoutingModel &model,
                                      const rows::SolverWrapper &solver_wrapper) const;

        std::shared_ptr<rows::Printer> printer_;

        rows::Problem problem_;
        operations_research::RoutingSearchParameters search_params_;
        osrm::EngineConfig routing_params_;
        std::string output_file_;
    };
}


#endif //ROWS_INCREMENTAL_WORKER_H
