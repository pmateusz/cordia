#ifndef ROWS_SOLVER_WRAPPER_H
#define ROWS_SOLVER_WRAPPER_H

#include <string>
#include <vector>

#include <boost/bimap.hpp>
#include <boost/optional.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/date_time.hpp>

#include <osrm/engine/engine_config.hpp>

#include <ortools/constraint_solver/routing.h>

#include "problem.h"
#include "location_container.h"
#include "scheduled_visit.h"
#include "calendar_visit.h"
#include "solution.h"
#include "route_validator.h"
#include "solver_wrapper.h"

namespace rows {

    class SolverWrapper {
    public:
        static const operations_research::RoutingModel::NodeIndex DEPOT;
        static const int64 SECONDS_IN_DAY;
        static const std::string TIME_DIMENSION;

        explicit SolverWrapper(const rows::Problem &problem, osrm::EngineConfig &config);

        void ConfigureModel(operations_research::RoutingModel &model);

        boost::posix_time::ptime::time_duration_type TravelTime(const Location &from, const Location &to);

        int64 Distance(operations_research::RoutingModel::NodeIndex from,
                       operations_research::RoutingModel::NodeIndex to);

        int64 ServiceTimePlusDistance(operations_research::RoutingModel::NodeIndex from,
                                      operations_research::RoutingModel::NodeIndex to);

        operations_research::RoutingModel::NodeIndex Index(const CalendarVisit &visit) const;

        operations_research::RoutingModel::NodeIndex Index(const ScheduledVisit &visit) const;

        boost::optional<operations_research::RoutingModel::NodeIndex> TryIndex(const CalendarVisit &visit) const;

        boost::optional<operations_research::RoutingModel::NodeIndex> TryIndex(const ScheduledVisit &visit) const;

        rows::CalendarVisit CalendarVisit(const operations_research::RoutingModel::NodeIndex visit) const;

        rows::Diary Diary(const operations_research::RoutingModel::NodeIndex carer) const;

        rows::Carer Carer(const operations_research::RoutingModel::NodeIndex carer) const;

        std::vector<operations_research::IntervalVar *> Breaks(operations_research::Solver *const solver,
                                                               const operations_research::RoutingModel::NodeIndex carer) const;

        std::vector<operations_research::RoutingModel::NodeIndex> Carers() const;

        const Location &depot() const;

        int NodesCount() const;

        int VehicleCount() const;

        void DisplayPlan(const operations_research::RoutingModel &routing,
                         const operations_research::Assignment &plan,
                         bool use_same_vehicle_costs,
                         int64 max_nodes_per_group,
                         int64 same_vehicle_cost,
                         const operations_research::RoutingDimension &time_dimension);

        static std::vector<rows::Location> GetUniqueLocations(const rows::Problem &problem);

        template<typename IteratorType>
        static Location GetCentralLocation(IteratorType begin_it, IteratorType end_it);

        const operations_research::RoutingSearchParameters &parameters() const;

        rows::Solution ResolveValidationErrors(const rows::Solution &solution,
                                               const rows::Problem &problem,
                                               const operations_research::RoutingModel &model);

        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> >
        GetNodeRoutes(const rows::Solution &solution, const operations_research::RoutingModel &model) const;

    private:
        enum class BreakType {
            BREAK, BEFORE_WORKDAY, AFTER_WORKDAY
        };

        rows::Solution Resolve(const rows::Solution &solution,
                               const std::vector<std::unique_ptr<rows::RouteValidator::ValidationError> > &validation_errors) const;

        struct PartialVisitOperations {
            std::size_t operator()(const rows::CalendarVisit &object) const noexcept;

            bool operator()(const rows::CalendarVisit &left, const rows::CalendarVisit &right) const noexcept;
        };

        void PrecomputeDistances();

        operations_research::RoutingSearchParameters CreateSearchParameters() const;

        static operations_research::IntervalVar *CreateBreak(operations_research::Solver *const solver,
                                                             const boost::posix_time::time_duration &start_time,
                                                             const boost::posix_time::time_duration &duration,
                                                             const std::string &label);

        static std::string
        GetBreakLabel(const operations_research::RoutingModel::NodeIndex carer, BreakType break_type);

        SolverWrapper(const rows::Problem &problem,
                      const std::vector<rows::Location> &locations,
                      osrm::EngineConfig &config);

        const rows::Problem &problem_;
        const Location depot_;
        rows::CachedLocationContainer location_container_;

        operations_research::RoutingSearchParameters parameters_;

        boost::bimaps::bimap<
                boost::bimaps::unordered_set_of<rows::CalendarVisit, PartialVisitOperations, PartialVisitOperations>,
                operations_research::RoutingModel::NodeIndex> visit_index_;
    };
}


#endif //ROWS_SOLVER_WRAPPER_H
