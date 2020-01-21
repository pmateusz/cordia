#ifndef ROWS_PROBLEM_DATA_H
#define ROWS_PROBLEM_DATA_H

#include <ortools/constraint_solver/routing_index_manager.h>

#include <boost/date_time.hpp>

#include "calendar_visit.h"

namespace rows {
    class Problem;

    class ProblemData {
    public:
        static const operations_research::RoutingNodeIndex DEPOT;

        virtual int vehicles() const = 0;

        virtual int nodes() const = 0;

        virtual boost::posix_time::time_duration VisitStart(operations_research::RoutingNodeIndex node) const = 0;

        virtual boost::posix_time::time_duration TotalWorkingHours(int vehicle, boost::gregorian::date date) const = 0;

        virtual int64 Distance(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const = 0;

        virtual int64 ServiceTime(operations_research::RoutingNodeIndex node) const = 0;

        virtual int64 ServicePlusTravelTime(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const = 0;

        virtual const std::vector<operations_research::RoutingNodeIndex> &GetNodes(const rows::CalendarVisit &visit) const = 0;

        virtual const std::vector<operations_research::RoutingNodeIndex> &GetNodes(operations_research::RoutingNodeIndex node) const = 0;

        virtual const rows::CalendarVisit &NodeToVisit(const operations_research::RoutingNodeIndex &node) const = 0;

        virtual boost::posix_time::ptime StartHorizon() const = 0;

        virtual boost::posix_time::ptime EndHorizon() const = 0;

        virtual bool Contains(const CalendarVisit &visit) const = 0;

        virtual const Problem &problem() const = 0;

        virtual int64 GetDroppedVisitPenalty() const = 0;
    };

    class ProblemDataFactory {
    public:
        virtual std::shared_ptr<ProblemData> makeProblem(Problem problem) const = 0;
    };
}

#endif //ROWS_PROBLEM_DATA_H
