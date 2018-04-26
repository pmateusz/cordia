#ifndef ROWS_MEMORYLIMITSEARCHMONITOR_H
#define ROWS_MEMORYLIMITSEARCHMONITOR_H

#include <ortools/constraint_solver/constraint_solver.h>

namespace rows {

    class MemoryLimitSearchMonitor : public operations_research::SearchLimit {
    public:
        MemoryLimitSearchMonitor(int64 memory_limit_in_bytes, operations_research::Solver *const solver);

        bool Check() override;

        void Init() override;

        void Copy(const SearchLimit *limit) override;

        operations_research::SearchLimit *MakeClone() const override;

    private:
        int64 memory_limit_in_bytes_;
    };
}

#endif //ROWS_MEMORYLIMITSEARCHMONITOR_H
