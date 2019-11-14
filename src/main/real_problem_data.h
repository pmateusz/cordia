#ifndef ROWS_REAL_PROBLEM_DATA_H
#define ROWS_REAL_PROBLEM_DATA_H

#include <unordered_map>
#include <memory>

#include <osrm/engine_config.hpp>

#include <ortools/constraint_solver/routing.h>

#include "problem.h"
#include "calendar_visit.h"
#include "location_container.h"

namespace rows {

    class Problem;

    class RealProblemData {
    public:
        static const operations_research::RoutingNodeIndex DEPOT;

        RealProblemData(Problem problem, std::unique_ptr<CachedLocationContainer> location_container);

        const std::vector<operations_research::RoutingNodeIndex> &GetNodes(const CalendarVisit &visit) const;

        const CalendarVisit &NodeToVisit(const operations_research::RoutingNodeIndex &node) const;

        std::pair<operations_research::RoutingNodeIndex, operations_research::RoutingNodeIndex> GetNodePair(const rows::CalendarVisit &visit) const;

        int vehicles() const;

        int nodes() const;

        int64 Distance(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to);

        int64 ServiceTime(operations_research::RoutingNodeIndex node);

        int64 ServicePlusTravelTime(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to);

    private:
        Problem problem_;
        std::unique_ptr<CachedLocationContainer> location_container_;

        std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, CalendarVisit> node_index_;
        std::unordered_map<CalendarVisit, std::vector<operations_research::RoutingIndexManager::NodeIndex> > visit_index_;
    };
}


#endif //ROWS_REAL_PROBLEM_DATA_H
