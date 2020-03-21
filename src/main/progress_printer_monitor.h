#ifndef ROWS_SEARCH_MONITOR_H
#define ROWS_SEARCH_MONITOR_H

#include <atomic>
#include <memory>

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

#include <printer.h>
#include "progress_monitor.h"
#include "declined_visit_evaluator.h"

namespace rows {

    class ProgressPrinterMonitor : public ProgressMonitor {
    public:
        ProgressPrinterMonitor(const operations_research::RoutingModel &model,
                               const operations_research::RoutingIndexManager &index_manager,
                               const ProblemData &problem_data,
                               std::shared_ptr<rows::Printer> printer);

        ProgressPrinterMonitor(const operations_research::RoutingModel &model,
                               const operations_research::RoutingIndexManager &index_manager,
                               const ProblemData &problem_data,
                               std::shared_ptr<rows::Printer> printer,
                               double cost_normalization_factor);

        virtual ~ProgressPrinterMonitor();

        bool AtSolution() override;

    private:
        std::shared_ptr<rows::Printer> printer_;

        DeclinedVisitEvaluator dropped_visit_evaluator_;
        double cost_normalization_factor_;
        double last_solution_cost_;
    };
}


#endif //ROWS_SEARCH_MONITOR_H
