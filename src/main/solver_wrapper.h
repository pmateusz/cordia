#ifndef ROWS_SOLVER_WRAPPER_H
#define ROWS_SOLVER_WRAPPER_H

#include <string>
#include <vector>

#include <osrm/engine/engine_config.hpp>

#include <ortools/constraint_solver/routing.h>

#include "problem.h"

#include "location_container.h"

namespace rows {

    class SolverWrapper {
    public:
        static const operations_research::RoutingModel::NodeIndex DEPOT;
        static const int64 SECONDS_IN_DAY;
        static const std::string TIME_DIMENSION;

        explicit SolverWrapper(const Problem &problem, osrm::EngineConfig &config);

        int64 Distance(operations_research::RoutingModel::NodeIndex from,
                       operations_research::RoutingModel::NodeIndex to);

        int64 ServiceTimePlusDistance(operations_research::RoutingModel::NodeIndex from,
                                      operations_research::RoutingModel::NodeIndex to);

        rows::Visit Visit(const operations_research::RoutingModel::NodeIndex visit) const;

        rows::Diary Diary(const operations_research::RoutingModel::NodeIndex carer) const;

        rows::Carer Carer(const operations_research::RoutingModel::NodeIndex carer) const;

        std::vector<operations_research::IntervalVar *> Breaks(operations_research::Solver *const solver,
                                                               const operations_research::RoutingModel::NodeIndex carer) const;

        std::vector<operations_research::RoutingModel::NodeIndex> Carers() const;

        int NodesCount() const;

        int VehicleCount() const;

        void DisplayPlan(const operations_research::RoutingModel &routing,
                         const operations_research::Assignment &plan,
                         bool use_same_vehicle_costs,
                         int64 max_nodes_per_group,
                         int64 same_vehicle_cost,
                         const operations_research::RoutingDimension &time_dimension);

        static std::vector<rows::Location> GetUniqueLocations(const rows::Problem &problem);

        void ComputeDistances();

        Location GetCentralLocation() const;

    private:
        enum class BreakType {
            BREAK, BEFORE_WORKDAY, AFTER_WORKDAY
        };

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
        rows::CachedLocationContainer location_container_;
    };
}


#endif //ROWS_SOLVER_WRAPPER_H
