#ifndef ROWS_ROUTING_H
#define ROWS_ROUTING_H

#include <ortools/constraint_solver/routing.h>

#include <boost/date_time.hpp>

namespace util {

    std::vector<std::vector<int64> > GetRoutes(const operations_research::RoutingModel &model);

    std::unordered_set<int64> GetVisitedNodes(const std::vector<std::vector<int64> > &routes, int64 depot_index);

    std::size_t GetDroppedVisitCount(const operations_research::RoutingModel &model);

    double Cost(const operations_research::RoutingModel &model);

    boost::posix_time::time_duration WallTime(operations_research::Solver const *solver);
}


#endif //ROWS_ROUTING_H
