#ifndef ROWS_ROUTING_OPERATIONS_H
#define ROWS_ROUTING_OPERATIONS_H

#include <vector>

#include <ortools/constraint_solver/routing.h>
#include <boost/algorithm/string/join.hpp>
#include "printer.h"

namespace rows {

    class RoutingOperations {
    public:
        int Remove(std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> &routes,
                   operations_research::RoutingModel::NodeIndex node) const;

        int Swap(std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> &routes,
                 operations_research::RoutingModel::NodeIndex left,
                 operations_research::RoutingModel::NodeIndex right) const;

        int Replace(std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> &routes,
                    operations_research::RoutingModel::NodeIndex from,
                    operations_research::RoutingModel::NodeIndex to,
                    std::size_t route_index) const;

        void PrintRoutes(std::shared_ptr<rows::Printer> printer,
                         const std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> &routes) const;
    };
}


#endif //ROWS_ROUTING_OPERATIONS_H
