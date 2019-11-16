#include <cstdlib>

#include <boost/format.hpp>
#include <boost/date_time.hpp>
#include <utility>

#include "progress_printer_monitor.h"
#include "util/routing.h"

namespace rows {

    ProgressPrinterMonitor::ProgressPrinterMonitor(const operations_research::RoutingModel &model, std::shared_ptr<rows::Printer> printer)
            : ProgressPrinterMonitor(model, printer, 1.0) {}

    ProgressPrinterMonitor::ProgressPrinterMonitor(const operations_research::RoutingModel &model,
                                                   std::shared_ptr<rows::Printer> printer,
                                                   double cost_normalization_factor)
            : ProgressMonitor(model),
              printer_(std::move(printer)),
              cost_normalization_factor_{cost_normalization_factor},
              last_solution_cost_{std::numeric_limits<double>::max()} {}

    ProgressPrinterMonitor::~ProgressPrinterMonitor() {
        printer_.reset();
    }

    bool ProgressPrinterMonitor::AtSolution() {
        const auto current_solution_cost = util::Cost(model());
        if (current_solution_cost >= last_solution_cost_) {
            return operations_research::SearchMonitor::AtSolution();
        }

        last_solution_cost_ = current_solution_cost;
        const auto wall_time = util::WallTime(solver());
        const auto memory_usage = operations_research::Solver::MemoryUsage();
        printer_->operator<<(ProgressStep(current_solution_cost * cost_normalization_factor_,
                                          util::GetDroppedVisitCount(model()),
                                          boost::posix_time::time_duration{wall_time.hours(),
                                                                           wall_time.minutes(),
                                                                           wall_time.seconds()},
                                          static_cast<size_t>(solver()->branches()),
                                          static_cast<size_t>(solver()->solutions()),
                                          static_cast<size_t>(memory_usage)));

        return operations_research::SearchMonitor::AtSolution();
    }
}
