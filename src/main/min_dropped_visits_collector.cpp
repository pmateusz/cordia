#include "util/routing.h"

#include <glog/logging.h>

#include "min_dropped_visits_collector.h"

rows::MinDroppedVisitsSolutionCollector::MinDroppedVisitsSolutionCollector(
        operations_research::RoutingModel const *model, bool abort_on_dropped_visits_increase)
        : SolutionCollector(model->solver()),
          model_{model},
          abort_on_dropped_visits_increase_{abort_on_dropped_visits_increase},
          min_cost_{kint64max},
          min_dropped_visits_{kint64max} {}

void rows::MinDroppedVisitsSolutionCollector::EnterSearch() {
    SolutionCollector::EnterSearch();

    min_cost_ = kint64max;
    min_dropped_visits_ = kint64max;
}

bool rows::MinDroppedVisitsSolutionCollector::AtSolution() {
    if (prototype_ != nullptr) {
        const auto dropped_visits = static_cast<int>(util::GetDroppedVisitCount(*model_));
        const operations_research::IntVar *objective = prototype_->Objective();

        if (abort_on_dropped_visits_increase_) {
            CHECK_LE(dropped_visits, min_dropped_visits_) << "The number of dropped visits increased in consecutive solutions";
        }

        if (dropped_visits < min_dropped_visits_) {
            if (objective != nullptr) {
                min_cost_ = objective->Min();
            }

            min_dropped_visits_ = dropped_visits;
            PopSolution();
            PushSolution();
        } else if (dropped_visits == min_dropped_visits_ && objective != nullptr && objective->Min() < min_cost_) {
            min_cost_ = objective->Min();
            min_dropped_visits_ = dropped_visits;
            PopSolution();
            PushSolution();
        }
    }
    return true;
}

std::string rows::MinDroppedVisitsSolutionCollector::DebugString() const {
    if (prototype_ == nullptr) {
        return "MinDroppedVisitsSolutionCollector()";
    } else {
        return "MinDroppedVisitsSolutionCollector(" + prototype_->DebugString() + ")";
    }
}


