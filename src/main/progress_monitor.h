#ifndef ROWS_PROGRESS_MONITOR_H
#define ROWS_PROGRESS_MONITOR_H

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

#include <boost/date_time/posix_time/posix_time_config.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>

namespace rows {

    class ProgressMonitor : public operations_research::SearchMonitor {
    public:
        explicit ProgressMonitor(const operations_research::RoutingModel &model);

    protected:
        std::size_t DroppedVisits() const;

        double Cost() const;

        boost::posix_time::time_duration WallTime() const;

    private:
        const operations_research::RoutingModel &model_;
    };
}


#endif //ROWS_PROGRESS_MONITOR_H
