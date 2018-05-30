#ifndef ROWS_PROGRESS_MONITOR_H
#define ROWS_PROGRESS_MONITOR_H

#include <ortools/constraint_solver/routing.h>

namespace rows {

    class ProgressMonitor : public operations_research::SearchMonitor {
    public:
        explicit ProgressMonitor(const operations_research::RoutingModel &model);

    protected:
        const operations_research::RoutingModel &model() const;

    private:
        const operations_research::RoutingModel &model_;
    };
}


#endif //ROWS_PROGRESS_MONITOR_H
