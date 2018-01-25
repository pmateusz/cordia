#ifndef ROWS_SOLVER_WRAPPER_H
#define ROWS_SOLVER_WRAPPER_H

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <boost/bimap.hpp>
#include <boost/bimap/unordered_set_of.hpp>
#include <boost/date_time.hpp>
#include <boost/optional.hpp>

#include <osrm/engine/engine_config.hpp>

#include <ortools/constraint_solver/routing.h>

#include "calendar_visit.h"
#include "carer.h"
#include "location_container.h"
#include "problem.h"
#include "route_validator.h"
#include "service_user.h"

namespace rows {

    class Solution;

    class ScheduledVisit;

    class SolverWrapper {
    public:

        class LocalServiceUser {
        public:
            LocalServiceUser();

            LocalServiceUser(const rows::ExtendedServiceUser &service_user, int64 visit_count);

            int64 Preference(const rows::Carer &carer) const;

            const rows::ExtendedServiceUser &service_user() const;

            int64 visit_count() const;

        private:
            rows::ExtendedServiceUser service_user_;
            int64 visit_count_;
        };

        static const operations_research::RoutingModel::NodeIndex DEPOT;
        static const int64 SECONDS_IN_DAY;
        static const std::string TIME_DIMENSION;
        static const int64 CARE_CONTINUITY_MAX;
        static const std::string CARE_CONTINUITY_DIMENSION;

        explicit SolverWrapper(const rows::Problem &problem, osrm::EngineConfig &config);

        void ConfigureModel(operations_research::RoutingModel &model);

        boost::posix_time::ptime::time_duration_type TravelTime(const Location &from, const Location &to);

        int64 Distance(operations_research::RoutingModel::NodeIndex from,
                       operations_research::RoutingModel::NodeIndex to);

        int64 ServicePlusTravelTime(operations_research::RoutingModel::NodeIndex from,
                                    operations_research::RoutingModel::NodeIndex to);

        int64 Preference(operations_research::RoutingModel::NodeIndex to, const rows::Carer &carer) const;

        operations_research::RoutingModel::NodeIndex Index(const CalendarVisit &visit) const;

        operations_research::RoutingModel::NodeIndex Index(const ScheduledVisit &visit) const;

        boost::optional<operations_research::RoutingModel::NodeIndex> TryIndex(const CalendarVisit &visit) const;

        boost::optional<operations_research::RoutingModel::NodeIndex> TryIndex(const ScheduledVisit &visit) const;

        rows::CalendarVisit CalendarVisit(operations_research::RoutingModel::NodeIndex visit) const;

        const LocalServiceUser &ServiceUser(operations_research::RoutingModel::NodeIndex visit) const;

        rows::Diary Diary(operations_research::RoutingModel::NodeIndex carer) const;

        rows::Carer Carer(operations_research::RoutingModel::NodeIndex carer) const;

        std::vector<operations_research::IntervalVar *> Breaks(operations_research::Solver *solver,
                                                               operations_research::RoutingModel::NodeIndex carer) const;

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

        std::vector<std::vector<std::pair<operations_research::RoutingModel::NodeIndex, rows::ScheduledVisit> > >
        GetRoutes(const rows::Solution &solution, const operations_research::RoutingModel &model) const;

        bool HasTimeWindows() const;

        int64 GetBeginWindow(boost::posix_time::time_duration value) const;

        int64 GetEndWindow(boost::posix_time::time_duration value) const;

    private:
        enum class BreakType {
            BREAK, BEFORE_WORKDAY, AFTER_WORKDAY
        };

        class CareContinuityMetrics {
        public:
            CareContinuityMetrics(SolverWrapper const *solver, rows::Carer carer);

            int64 operator()(operations_research::RoutingModel::NodeIndex from,
                             operations_research::RoutingModel::NodeIndex to) const;

        private:
            SolverWrapper const *solver_;
            rows::Carer carer_;
        };

        rows::Solution Resolve(const rows::Solution &solution,
                               const std::vector<std::unique_ptr<rows::RouteValidator::ValidationError> > &validation_errors) const;

        struct PartialVisitOperations {
            std::size_t operator()(const rows::CalendarVisit &object) const noexcept;

            bool operator()(const rows::CalendarVisit &left, const rows::CalendarVisit &right) const noexcept;
        };

        operations_research::RoutingSearchParameters CreateSearchParameters() const;

        operations_research::IntervalVar *CreateBreak(operations_research::Solver *solver,
                                                      const boost::posix_time::time_duration &start_time,
                                                      const boost::posix_time::time_duration &duration,
                                                      const std::string &label) const;

        operations_research::IntervalVar *CreateBreakWithTimeWindows(operations_research::Solver *solver,
                                                                     const boost::posix_time::time_duration &start_time,
                                                                     const boost::posix_time::time_duration &duration,
                                                                     const std::string &label) const;

        static std::string GetBreakLabel(operations_research::RoutingModel::NodeIndex carer, BreakType break_type);

        SolverWrapper(const rows::Problem &problem,
                      const std::vector<rows::Location> &locations,
                      osrm::EngineConfig &config);

        const rows::Problem &problem_;
        const Location depot_;
        const LocalServiceUser depot_service_user_;
        boost::posix_time::time_duration time_window_;
        rows::CachedLocationContainer location_container_;

        operations_research::RoutingSearchParameters parameters_;

        std::unordered_map<rows::ServiceUser, rows::SolverWrapper::LocalServiceUser> service_users_;

        boost::bimaps::bimap<
                boost::bimaps::unordered_set_of<rows::CalendarVisit, PartialVisitOperations, PartialVisitOperations>,
                operations_research::RoutingModel::NodeIndex> visit_index_;

        std::vector<CareContinuityMetrics> care_continuity_metrics_;
    };
}


#endif //ROWS_SOLVER_WRAPPER_H
