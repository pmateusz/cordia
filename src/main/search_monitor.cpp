#include <cstdlib>

#include <glog/logging.h>
#include <boost/format.hpp>
#include <boost/date_time.hpp>

#include "search_monitor.h"


namespace rows {
    SearchMonitor::SearchMonitor(operations_research::Solver *const solver,
                                 operations_research::RoutingModel *const model,
                                 const std::shared_ptr<rows::Printer> &printer,
                                 const std::atomic<bool> &cancel_token)
            : operations_research::SearchMonitor(solver),
              printer_(printer),
              model_{model},
              cancel_token_{cancel_token} {}

    SearchMonitor::~SearchMonitor() {
        printer_.reset();
    }

    bool SearchMonitor::AtSolution() {
        const auto wall_time = WallTime();
        printer_->operator<<(ProgressStep(Cost(),
                                          DroppedVisits(),
                                          boost::posix_time::time_duration{wall_time.hours(),
                                                                           wall_time.minutes(),
                                                                           wall_time.seconds()},
                                          static_cast<size_t>(solver()->branches()),
                                          static_cast<size_t>(solver()->solutions()),
                                          static_cast<size_t>(solver()->MemoryUsage())));

        return operations_research::SearchMonitor::AtSolution();
    }

    std::size_t SearchMonitor::DroppedVisits() const {
        std::size_t dropped_visits = 0;
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

    void SearchMonitor::BeginNextDecision(operations_research::DecisionBuilder *const b) {
        PeriodicCheck();
        TopPeriodicCheck();
    }

    void SearchMonitor::RefuteDecision(operations_research::Decision *const d) {
        PeriodicCheck();
        TopPeriodicCheck();
    }

    void SearchMonitor::PeriodicCheck() {
        if (crossed_ || cancel_token_) {
            crossed_ = true;
            solver()->Fail();
        }
    }

    void SearchMonitor::TopPeriodicCheck() {
        if (solver()->SolveDepth() > 0) {
            solver()->TopPeriodicCheck();
        }
    }
}
