#ifndef ROWS_MEMORYLIMITSEARCHMONITOR_H
#define ROWS_MEMORYLIMITSEARCHMONITOR_H

#include <ortools/constraint_solver/constraint_solver.h>

namespace rows {

    class MemoryLimitSearchMonitor : public operations_research::SearchLimit {
    public:
        MemoryLimitSearchMonitor(operations_research::Solver *const solver);

    };
}

#endif //ROWS_MEMORYLIMITSEARCHMONITOR_H
