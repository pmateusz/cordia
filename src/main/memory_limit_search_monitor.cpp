#include "memory_limit_search_monitor.h"

rows::MemoryLimitSearchMonitor::MemoryLimitSearchMonitor(operations_research::Solver *const solver)
        : SearchLimit(solver) {}
