#ifndef ROWS_ROUTING_OPERATIONS_H
#define ROWS_ROUTING_OPERATIONS_H

#include <vector>

#include <ortools/constraint_solver/routing.h>
#include <boost/algorithm/string/join.hpp>
#include "printer.h"

namespace rows {

    class RoutingOperations {
    public:
        int Remove(std::vector<std::vector<int64> > &routes, int64 node) const;

        int Swap(std::vector<std::vector<int64> > &routes, int64 left, int64 right) const;

        int Replace(std::vector<std::vector<int64> > &routes,
                    int64 from, int64 to, std::size_t route_index) const;

        void PrintRoutes(std::shared_ptr<rows::Printer> printer,
                         const std::vector<std::vector<int64> > &routes) const;
    };
}


#endif //ROWS_ROUTING_OPERATIONS_H
