#include "solution_log_monitor.h"

#include <glog/logging.h>

#include "util/routing.h"

rows::SolutionLogMonitor::SolutionLogMonitor(operations_research::RoutingIndexManager const *index_manager,
                                             operations_research::RoutingModel const *model,
                                             std::shared_ptr<rows::SolutionRepository> solution_repository)
        : SearchLimit(model->solver()),
          index_manager_{index_manager},
          model_{model},
          min_dropped_visits_{std::numeric_limits<int>::max()},
          dropped_visits_buffer_{5},
          cut_off_threshold_{2},
          stop_search_{false},
          solution_repository_{solution_repository} {}

bool rows::SolutionLogMonitor::AtSolution() {
    const auto routes = util::GetRoutes(*model_);
    const auto dropped_visits_count = static_cast<int>(util::GetDroppedVisitCount(*model_));
    CHECK_EQ(model_->nodes(),
             dropped_visits_count + util::GetVisitedNodes(routes, model_->GetDepot()).size() + 1);

    if (dropped_visits_count <= min_dropped_visits_) {
        min_dropped_visits_ = dropped_visits_count;
        solution_repository_->Store(routes);
    }
    dropped_visits_buffer_.push_back(dropped_visits_count);

    const auto descent = dropped_visits_buffer_.back() == min_dropped_visits_;
    if (descent) {
        return operations_research::SearchLimit::AtSolution();
    }

    const auto find_it = std::find(std::rbegin(dropped_visits_buffer_),
                                   std::rend(dropped_visits_buffer_),
                                   min_dropped_visits_);
    if (find_it == std::rend(dropped_visits_buffer_)) {
        stop_search_ = true;
        return stop_search_;
    }

    const auto distance = std::distance(std::rbegin(dropped_visits_buffer_), find_it);
    stop_search_ = distance > cut_off_threshold_;
    return stop_search_;
}

void rows::SolutionLogMonitor::EnterSearch() {
    SearchLimit::EnterSearch();

    stop_search_ = false;
    min_dropped_visits_ = std::numeric_limits<int>::max();
    dropped_visits_buffer_.clear();
}

bool rows::SolutionLogMonitor::Check() {
    return stop_search_;
}

void rows::SolutionLogMonitor::Init() {}

void rows::SolutionLogMonitor::Copy(const operations_research::SearchLimit *limit) {
    auto prototype_limit_ptr = reinterpret_cast<const SolutionLogMonitor *>(limit);
    model_ = prototype_limit_ptr->model_;
    solution_repository_ = prototype_limit_ptr->solution_repository_;
    min_dropped_visits_ = prototype_limit_ptr->min_dropped_visits_;
    dropped_visits_buffer_ = prototype_limit_ptr->dropped_visits_buffer_;
    cut_off_threshold_ = prototype_limit_ptr->cut_off_threshold_;
    stop_search_ = prototype_limit_ptr->stop_search_;
}

operations_research::SearchLimit *rows::SolutionLogMonitor::MakeClone() const {
    return solver()->RevAlloc(new SolutionLogMonitor(index_manager_, model_, solution_repository_));
}
