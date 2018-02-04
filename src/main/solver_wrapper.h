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

    class ScheduledVisit;

    class Solution;

    class SolverWrapper {
    public:

        class LocalServiceUser {
        public:
            LocalServiceUser();

            LocalServiceUser(const rows::ExtendedServiceUser &service_user, int64 visit_count);

            int64 Preference(const rows::Carer &carer) const;

            bool IsPreferred(const rows::Carer &carer) const;

            const rows::ExtendedServiceUser &service_user() const;

            int64 visit_count() const;

        private:
            rows::ExtendedServiceUser service_user_;
            int64 visit_count_;
        };

        struct Statistics {
            double Cost{0.0};
            std::size_t DroppedVisits{0};
            std::size_t Errors{0};

            struct {
                double Mean{0.0};
                double Median{0.0};
                double Stddev{0.0};
            } CareContinuity;

            struct {
                double Mean{0.0};
                double Median{0.0};
                double Stddev{0.0};
                double TotalMean{0.0};
            } CarerUtility;
        };

        static const operations_research::RoutingModel::NodeIndex DEPOT;
        static const int64 SECONDS_IN_DAY;
        static const std::string TIME_DIMENSION;
        static const int64 CARE_CONTINUITY_MAX;
        static const std::string CARE_CONTINUITY_DIMENSION;

        explicit SolverWrapper(const rows::Problem &problem, osrm::EngineConfig &config);

        void ConfigureModel(operations_research::RoutingModel &model);

        Statistics CalculateStats(const operations_research::RoutingModel &model,
                                  const operations_research::Assignment &solution);

        int64 Distance(operations_research::RoutingModel::NodeIndex from,
                       operations_research::RoutingModel::NodeIndex to);

        int64 ServicePlusTravelTime(operations_research::RoutingModel::NodeIndex from,
                                    operations_research::RoutingModel::NodeIndex to);

        operations_research::RoutingModel::NodeIndex VisitToNode(const CalendarVisit &visit) const;

        operations_research::RoutingModel::NodeIndex VisitToNode(const ScheduledVisit &visit) const;

        boost::optional<operations_research::RoutingModel::NodeIndex> TryVisitToNode(const CalendarVisit &visit) const;

        boost::optional<operations_research::RoutingModel::NodeIndex> TryVisitToNode(const ScheduledVisit &visit) const;

        rows::CalendarVisit NodeToVisit(operations_research::RoutingModel::NodeIndex visit) const;

        const LocalServiceUser &User(const rows::ServiceUser &service_user) const;

        const rows::Carer &Carer(int vehicle) const;

        operations_research::IntVar const *CareContinuityVar(const rows::ExtendedServiceUser &service_user) const;

        std::vector<operations_research::IntervalVar *> CreateBreakIntervals(operations_research::Solver *solver,
                                                                             const rows::Carer &carer,
                                                                             const rows::Diary &diary) const;

        void DisplayPlan(const operations_research::RoutingModel &routing, const operations_research::Assignment &plan);

        Solution ResolveValidationErrors(const rows::Solution &solution,
                                         const rows::Problem &problem,
                                         const operations_research::RoutingModel &model);

        std::vector<std::vector<std::pair<operations_research::RoutingModel::NodeIndex, rows::ScheduledVisit> > >
        GetRoutes(const rows::Solution &solution, const operations_research::RoutingModel &model) const;

        int64 GetBeginWindow(boost::posix_time::time_duration value) const;

        int64 GetEndWindow(boost::posix_time::time_duration value) const;

        const Location &depot() const;

        const Problem &problem() const;

        const operations_research::RoutingSearchParameters &parameters() const;

        bool HasTimeWindows() const;

    private:
        enum class BreakType {
            BREAK, BEFORE_WORKDAY, AFTER_WORKDAY
        };

        class CareContinuityMetrics {
        public:
            CareContinuityMetrics(const SolverWrapper &solver, const rows::Carer &carer);

            int64 operator()(operations_research::RoutingModel::NodeIndex from,
                             operations_research::RoutingModel::NodeIndex to) const;

        private:
            std::unordered_map<operations_research::RoutingModel::NodeIndex, int64> values_;
        };

        rows::Solution Resolve(const rows::Solution &solution,
                               const std::vector<std::unique_ptr<rows::RouteValidator::ValidationError> > &validation_errors) const;

        struct PartialVisitOperations {
            std::size_t operator()(const rows::CalendarVisit &object) const noexcept;

            bool operator()(const rows::CalendarVisit &left, const rows::CalendarVisit &right) const noexcept;
        };

        operations_research::RoutingSearchParameters CreateSearchParameters() const;

        operations_research::IntervalVar *CreateBreakWithTimeWindows(operations_research::Solver *solver,
                                                                     const boost::posix_time::time_duration &start_time,
                                                                     const boost::posix_time::time_duration &duration,
                                                                     const std::string &label) const;

        static std::string GetBreakLabel(const rows::Carer &carer, BreakType break_type);

        SolverWrapper(const rows::Problem &problem,
                      const std::vector<rows::Location> &locations,
                      osrm::EngineConfig &config);

        const rows::Problem &problem_;
        const Location depot_;
        const LocalServiceUser depot_service_user_;
        boost::posix_time::time_duration visit_time_window_;
        rows::CachedLocationContainer location_container_;

        operations_research::RoutingSearchParameters parameters_;

        std::unordered_map<rows::ServiceUser, rows::SolverWrapper::LocalServiceUser> service_users_;

        boost::bimaps::bimap<
                boost::bimaps::unordered_set_of<rows::CalendarVisit, PartialVisitOperations, PartialVisitOperations>,
                operations_research::RoutingModel::NodeIndex> visit_index_;

        std::unordered_map<rows::ExtendedServiceUser, operations_research::IntVar *> care_continuity_;

        std::vector<CareContinuityMetrics> care_continuity_metrics_;
    };
}


#endif //ROWS_SOLVER_WRAPPER_H
