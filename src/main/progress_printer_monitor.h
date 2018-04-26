#ifndef ROWS_SEARCH_MONITOR_H
#define ROWS_SEARCH_MONITOR_H

#include <atomic>
#include <memory>

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

#include <printer.h>
#include "progress_monitor.h"

namespace rows {

    class ProgressPrinterMonitor : public ProgressMonitor {
    public:
        ProgressPrinterMonitor(const operations_research::RoutingModel &model,
                               std::shared_ptr <rows::Printer> printer);

        virtual ~ProgressPrinterMonitor();

        bool AtSolution() override;

    private:
        std::shared_ptr <rows::Printer> printer_;
    };
}


#endif //ROWS_SEARCH_MONITOR_H
