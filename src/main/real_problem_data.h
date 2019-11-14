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
        static const int64 SECONDS_IN_DIMENSION;

        RealProblemData(Problem problem, std::unique_ptr<CachedLocationContainer> location_container);

        const std::vector<operations_research::RoutingNodeIndex> &GetNodes(const CalendarVisit &visit) const;

        const std::vector<operations_research::RoutingNodeIndex> &GetNodes(operations_research::RoutingNodeIndex node) const;

        const CalendarVisit &NodeToVisit(const operations_research::RoutingNodeIndex &node) const;

        std::pair<operations_research::RoutingNodeIndex, operations_research::RoutingNodeIndex> GetNodePair(const rows::CalendarVisit &visit) const;

        int vehicles() const;

        int nodes() const;

        boost::posix_time::ptime StartHorizon() const {
            return start_horizon_;
        }

        boost::posix_time::ptime EndHorizon() const {
            return start_horizon_ + boost::posix_time::seconds(SECONDS_IN_DIMENSION);
        }

        boost::posix_time::time_duration VisitStart(operations_research::RoutingNodeIndex node) const;

        boost::posix_time::time_duration TotalWorkingHours(int vehicle, boost::gregorian::date date) const;

        int64 GetDroppedVisitPenalty() const;

        int64 Distance(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const;

        int64 ServiceTime(operations_research::RoutingNodeIndex node) const;

        int64 ServicePlusTravelTime(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const;

        bool Contains(const CalendarVisit &visit) const;

        const Problem &problem() const { return problem_; }

    private:
        Problem problem_;

        std::unique_ptr<CachedLocationContainer> location_container_;

        boost::posix_time::ptime start_horizon_;

        std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, CalendarVisit> node_index_;
        std::unordered_map<CalendarVisit,
                std::vector<operations_research::RoutingIndexManager::NodeIndex>,
                Problem::PartialVisitOperations,
                Problem::PartialVisitOperations> visit_index_;
    };

    class RealProblemDataFactory {
    public:
        explicit RealProblemDataFactory(osrm::EngineConfig engine_config);

        std::shared_ptr<RealProblemData> operator()(Problem problem) const;

    private:
        osrm::EngineConfig engine_config_;
    };
}


#endif //ROWS_REAL_PROBLEM_DATA_H
