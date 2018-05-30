#include <cstdlib>

#include <glog/logging.h>
#include <boost/format.hpp>
#include <boost/date_time.hpp>
#include <utility>

#include "progress_printer_monitor.h"
#include "util/routing.h"

namespace rows {

    ProgressPrinterMonitor::ProgressPrinterMonitor(const operations_research::RoutingModel &model,
                                                   std::shared_ptr<rows::Printer> printer)
            : ProgressMonitor(model),
              printer_(std::move(printer)) {}

    ProgressPrinterMonitor::~ProgressPrinterMonitor() {
        printer_.reset();
    }

    bool ProgressPrinterMonitor::AtSolution() {
        const auto wall_time = util::WallTime(solver());
        const auto memory_usage = operations_research::Solver::MemoryUsage();
        printer_->operator<<(ProgressStep(util::Cost(model()),
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
