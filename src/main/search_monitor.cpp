#include <cstdlib>

#include <glog/logging.h>
#include <boost/format.hpp>
#include <boost/date_time.hpp>

#include "search_monitor.h"


static std::string HumanReadableSize(long bytes) {
    static const auto UNIT = 1024;
    if (bytes < UNIT) {
        return std::to_string(bytes) + " B";
    }

    auto exp = static_cast<int>(std::log(bytes) / std::log(UNIT));
    auto prefix = "KMGTPE"[exp - 1];
    return (boost::format("%.1f %sB") % (bytes / std::pow(UNIT, exp)) % prefix).str();
}

namespace rows {

    SearchMonitor::SearchMonitor(operations_research::Solver *const solver,
                                 operations_research::RoutingModel *const model)
            : operations_research::SearchMonitor(solver),
              model_{model} {
    }

    bool SearchMonitor::AtSolution() {
        const auto wall_time = WallTime();
        boost::posix_time::time_duration wall_time_to_use{wall_time.hours(), wall_time.minutes(), wall_time.seconds()};

        LOG(INFO) << boost::format("%10e | %14d | %9s | %8d | %9d | %12s")
                     % Cost()
                     % DroppedVisits()
                     % wall_time_to_use
                     % solver()->branches()
                     % solver()->solutions()
                     % HumanReadableSize(solver()->MemoryUsage());

        return operations_research::SearchMonitor::AtSolution();
    }

    void SearchMonitor::EnterSearch() {
        LOG(INFO) << boost::format("%12s | %10s | %5s | %5s | %5s | %12s")
                     % "Cost"
                     % "Dropped Visits"
                     % "Wall Time"
                     % "Branches"
                     % "Solutions"
                     % "Memory Usage";

        operations_research::SearchMonitor::EnterSearch();
    }

    int SearchMonitor::DroppedVisits() const {
        auto dropped_visits = 0;
        for (int order = 1; order < model_->nodes(); ++order) {
            if (model_->NextVar(order)->Value() == order) {
                ++dropped_visits;
            }
        }
        return dropped_visits;
    }

    double SearchMonitor::Cost() const {
        return static_cast<double>(model_->CostVar()->Value());
    }

    boost::posix_time::time_duration SearchMonitor::WallTime() const {
        return boost::posix_time::milliseconds(static_cast<int64_t>(solver()->wall_time()));
    }
}
