#include "progress_monitor.h"

rows::ProgressMonitor::ProgressMonitor(const operations_research::RoutingModel &model)
        : SearchMonitor(model.solver()),
          model_(model) {}

std::size_t rows::ProgressMonitor::DroppedVisits() const {
    std::size_t dropped_visits = 0;
    for (int order = 1; order < model_.nodes(); ++order) {
        if (model_.NextVar(order)->Value() == order) {
            ++dropped_visits;
        }
    }
    return dropped_visits;
}

double rows::ProgressMonitor::Cost() const {
    return static_cast<double>(model_.CostVar()->Value());
}

boost::posix_time::time_duration rows::ProgressMonitor::WallTime() const {
    return boost::posix_time::milliseconds(static_cast<int64_t>(solver()->wall_time()));
}
