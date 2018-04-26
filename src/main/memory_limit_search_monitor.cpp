#include "memory_limit_search_monitor.h"

rows::MemoryLimitSearchMonitor::MemoryLimitSearchMonitor(int64 memory_limit_in_bytes,
                                                         operations_research::Solver *const solver)
        : SearchLimit(solver),
          memory_limit_in_bytes_(memory_limit_in_bytes) {}

bool rows::MemoryLimitSearchMonitor::Check() {
    return operations_research::Solver::MemoryUsage() >= memory_limit_in_bytes_;
}

void rows::MemoryLimitSearchMonitor::Init() {}

operations_research::SearchLimit *rows::MemoryLimitSearchMonitor::MakeClone() const {
    return solver()->RevAlloc(new MemoryLimitSearchMonitor(memory_limit_in_bytes_, solver()));
}

void rows::MemoryLimitSearchMonitor::Copy(const SearchLimit *limit) {
    auto limit_prototype_ptr = reinterpret_cast<const MemoryLimitSearchMonitor *>(limit);
    memory_limit_in_bytes_ = limit_prototype_ptr->memory_limit_in_bytes_;
}
