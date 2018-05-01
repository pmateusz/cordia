#ifndef ROWS_SINGLE_STEP_WORKER_H
#define ROWS_SINGLE_STEP_WORKER_H

#include <memory>
#include <string>

#include <ortools/constraint_solver/routing.h>

#include <nlohmann/json.hpp>

#include <boost/exception/diagnostic_information.hpp>

#include "scheduling_worker.h"
#include "solution.h"
#include "printer.h"
#include "single_step_solver.h"
#include "gexf_writer.h"

namespace rows {

    class SingleStepSchedulingWorker : public rows::SchedulingWorker {
    public:
        explicit SingleStepSchedulingWorker(std::shared_ptr<rows::Printer> printer);

        ~SingleStepSchedulingWorker() override;

        bool Init(rows::Problem problem,
                  osrm::EngineConfig engine_config,
                  boost::optional<rows::Solution> past_solution,
                  operations_research::RoutingSearchParameters search_parameters,
                  std::string output_file);

    private:
        void Run() override;

        std::string output_file_;

        std::shared_ptr<rows::Printer> printer_;

        operations_research::Assignment *initial_assignment_;
        std::unique_ptr<operations_research::RoutingModel> model_;
        std::unique_ptr<rows::SolverWrapper> solver_;
    };
}


#endif //ROWS_SINGLE_STEP_WORKER_H
