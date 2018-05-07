#ifndef ROWS_EXPERIMENTAL_ENFORCEMENT_WORKER_H
#define ROWS_EXPERIMENTAL_ENFORCEMENT_WORKER_H

#include <exception>
#include <memory>
#include <iterator>
#include <string>
#include <atomic>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/date_time.hpp>
#include <boost/algorithm/string/join.hpp>

#include <ortools/constraint_solver/routing.h>

#include <osrm/engine/engine_config.hpp>

#include <algorithm>

#include "scheduling_worker.h"
#include "printer.h"
#include "solver_wrapper.h"

namespace rows {

    class ExperimentalEnforcementWorker : public rows::SchedulingWorker {
    public:
        class Solver : public SolverWrapper {
        public:
            Solver(const rows::Problem &problem,
                   osrm::EngineConfig &config,
                   const operations_research::RoutingSearchParameters &search_parameters,
                   boost::posix_time::time_duration break_time_window,
                   bool begin_end_work_day_adjustment_enabled);

            void ConfigureModel(operations_research::RoutingModel &model,
                                const std::shared_ptr<Printer> &printer,
                                std::shared_ptr<const std::atomic<bool> > cancel_token) override;
        };

        explicit ExperimentalEnforcementWorker(std::shared_ptr<rows::Printer> printer);

        void Run() override;

        bool Init(rows::Problem problem,
                  osrm::EngineConfig routing_params,
                  const operations_research::RoutingSearchParameters &search_params,
                  std::string output_file);

    private:
        std::shared_ptr<rows::Printer> printer_;

        rows::Problem problem_;
        operations_research::RoutingSearchParameters search_params_;
        osrm::EngineConfig routing_params_;
        std::string output_file_;

        double progress_fraction_{0.2};
        int halt_restarts_{5};
    };
}


#endif //ROWS_EXPERIMENTAL_ENFORCEMENT_WORKER_H
