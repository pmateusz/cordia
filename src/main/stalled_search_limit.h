#ifndef ROWS_STALLED_SEARCH_LIMIT_H
#define ROWS_STALLED_SEARCH_LIMIT_H

#include <ortools/constraint_solver/constraint_solver.h>

namespace rows {

    class StalledSearchLimit : public operations_research::SearchLimit {
    public:
        StalledSearchLimit(int64 time_limit_ms, operations_research::Solver *solver);

        bool Check() override;

        void Init() override;

        void Copy(const SearchLimit *limit) override;

        operations_research::SearchLimit *MakeClone() const override;

        void EnterSearch() override;

        void ExitSearch() override;

        bool AtSolution() override;

    private:
        bool search_in_progress_{false};
        bool found_first_solution_{false};
        int64 last_solution_update_{0};

        int64 time_limit_ms_;
    };
}


#endif //ROWS_STALLED_SEARCH_LIMIT_H
