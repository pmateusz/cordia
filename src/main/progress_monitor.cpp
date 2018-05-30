#include "progress_monitor.h"

rows::ProgressMonitor::ProgressMonitor(const operations_research::RoutingModel &model)
        : SearchMonitor(model.solver()),
          model_(model) {}

const operations_research::RoutingModel &rows::ProgressMonitor::model() const {
    return model_;
}
