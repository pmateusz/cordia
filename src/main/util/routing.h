#ifndef ROWS_ROUTING_H
#define ROWS_ROUTING_H

#include <ortools/constraint_solver/routing.h>

#include <boost/date_time.hpp>

namespace util {

    std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > GetRoutes(
            const operations_research::RoutingModel &model);

    std::unordered_set<operations_research::RoutingModel::NodeIndex> GetVisitedNodes(
            const std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > &routes,
            const operations_research::RoutingModel::NodeIndex depot_index);

    std::size_t GetDroppedVisitCount(const operations_research::RoutingModel &model);

    double Cost(const operations_research::RoutingModel &model);

    boost::posix_time::time_duration WallTime(operations_research::Solver const *solver);
}


#endif //ROWS_ROUTING_H
