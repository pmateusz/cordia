#include "stalled_search_limit.h"

rows::StalledSearchLimit::StalledSearchLimit(int64 time_limit_ms, operations_research::Solver *const solver)
        : SearchLimit(solver),
          time_limit_ms_(time_limit_ms) {}

bool rows::StalledSearchLimit::Check() {
    return found_first_solution_ && search_in_progress_ &&
           (solver()->wall_time() - last_solution_update_) > time_limit_ms_;
}

void rows::StalledSearchLimit::Init() {}

void rows::StalledSearchLimit::Copy(const operations_research::SearchLimit *limit) {
    auto prototype_limit_to_use = reinterpret_cast<const rows::StalledSearchLimit *>(limit);
    last_solution_update_ = prototype_limit_to_use->last_solution_update_;
    found_first_solution_ = prototype_limit_to_use->found_first_solution_;
    search_in_progress_ = prototype_limit_to_use->search_in_progress_;
}

operations_research::SearchLimit *rows::StalledSearchLimit::MakeClone() const {
    return solver()->RevAlloc(new StalledSearchLimit(time_limit_ms_, solver()));
}

bool rows::StalledSearchLimit::AtSolution() {
    last_solution_update_ = solver()->wall_time();
    found_first_solution_ = true;

    return true;
}

void rows::StalledSearchLimit::EnterSearch() {
    last_solution_update_ = solver()->wall_time();
    search_in_progress_ = true;

    operations_research::SearchLimit::EnterSearch();
}

void rows::StalledSearchLimit::ExitSearch() {
    search_in_progress_ = false;

    operations_research::SearchLimit::ExitSearch();
}
