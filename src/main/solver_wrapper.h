#ifndef ROWS_SOLVER_WRAPPER_H
#define ROWS_SOLVER_WRAPPER_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <boost/bimap.hpp>
#include <boost/bimap/vector_of.hpp>
#include <boost/date_time.hpp>
#include <boost/optional.hpp>

#include <osrm/engine/engine_config.hpp>

#include <ortools/constraint_solver/routing.h>
#include <ortools/base/logging.h>
#include <ortools/constraint_solver/constraint_solver.h>
#include <boost/date_time/posix_time/posix_time_config.hpp>

#include "calendar_visit.h"
#include "carer.h"
#include "location_container.h"
#include "problem.h"
#include "route_validator.h"
#include "service_user.h"
#include "printer.h"
#include "real_problem_data.h"

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
            std::size_t TotalVisits{0};
            std::size_t Errors{0};

            struct {
                double Mean{0.0};
                double Median{0.0};
                double Stddev{0.0};
                double TotalMean{0.0};
            } CarerUtility;

            virtual std::string RenderDescription() const;
        };

        static const std::string TIME_DIMENSION;
        static const int64 MAX_CARERS_SINGLE_VISITS;
        static const int64 MAX_CARERS_MULTIPLE_VISITS;

        SolverWrapper(const rows::ProblemData &problem_data,
                      const operations_research::RoutingSearchParameters &search_parameters);

        SolverWrapper(const rows::ProblemData &problem_data,
                      const operations_research::RoutingSearchParameters &search_parameters,
                      boost::posix_time::time_duration visit_time_window,
                      boost::posix_time::time_duration break_time_window,
                      boost::posix_time::time_duration begin_end_work_day_adjustment);

        virtual void ConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                                    operations_research::RoutingModel &model,
                                    const std::shared_ptr<Printer> &printer,
                                    std::shared_ptr<const std::atomic<bool> > cancel_token) = 0;

        virtual std::string GetDescription(const operations_research::RoutingIndexManager &index_manager,
                                           const operations_research::RoutingModel &model,
                                           const operations_research::Assignment &solution);

        int64 Distance(operations_research::RoutingNodeIndex from,
                       operations_research::RoutingNodeIndex to);

        int64 ServiceTime(operations_research::RoutingNodeIndex node);

        int64 ServicePlusTravelTime(operations_research::RoutingNodeIndex from,
                                    operations_research::RoutingNodeIndex to);

        bool Contains(const CalendarVisit &visit) const;

        const LocalServiceUser &User(const rows::ServiceUser &service_user) const;

        const rows::Carer &Carer(int vehicle) const;

        int Vehicle(const rows::Carer &carer) const;

        const boost::posix_time::ptime EndHorizon() const;

        const boost::posix_time::ptime StartHorizon() const;

        const Location &depot() const;

        const Problem &problem() const;

        const operations_research::RoutingSearchParameters &parameters() const;

        bool HasTimeWindows() const;

        int vehicles() const;

        int nodes() const;

        const std::vector<operations_research::RoutingNodeIndex> &GetNodes(const CalendarVisit &visit) const;

        const std::vector<operations_research::RoutingNodeIndex> &GetNodes(const ScheduledVisit &visit) const;

        std::pair<operations_research::RoutingNodeIndex,
                operations_research::RoutingNodeIndex> GetNodePair(const rows::CalendarVisit &visit) const;

        int64 GetBeginVisitWindow(boost::posix_time::time_duration value) const;

        int64 GetEndVisitWindow(boost::posix_time::time_duration value) const;

        int64 GetBeginBreakWindow(boost::posix_time::time_duration value) const;

        int64 GetEndBreakWindow(boost::posix_time::time_duration value) const;

        std::vector<rows::Event> GetEffectiveBreaks(const rows::Diary &diary) const;

        boost::gregorian::date GetScheduleDate() const;

        boost::posix_time::time_duration GetAdjustment() const;

        const CalendarVisit &NodeToVisit(const operations_research::RoutingNodeIndex &node) const;

        void DisplayPlan(const operations_research::RoutingIndexManager &index_manager,
                         const operations_research::RoutingModel &routing,
                         const operations_research::Assignment &plan);

        Solution ResolveValidationErrors(const rows::Solution &solution,
                                         const operations_research::RoutingIndexManager &index_manager,
                                         const operations_research::RoutingModel &model);

        std::vector<std::vector<int64> > GetRoutes(const rows::Solution &solution,
                                                   const operations_research::RoutingIndexManager &index_manager,
                                                   const operations_research::RoutingModel &model) const;

        std::string GetModelStatus(int status);

        int64 GetDroppedVisitPenalty();

        bool out_office_hours_breaks_enabled() const;

    protected:
        void OnConfigureModel(const operations_research::RoutingIndexManager &index_manager,
                              const operations_research::RoutingModel &model);

        void AddTravelTime(operations_research::Solver *solver,
                           operations_research::RoutingModel &model,
                           const operations_research::RoutingIndexManager &index_manager);

        void AddVisitsHandling(operations_research::Solver *solver,
                               operations_research::RoutingModel &model,
                               const operations_research::RoutingIndexManager &index_manager);

        void AddCarerHandling(operations_research::Solver *solver,
                              operations_research::RoutingModel &model,
                              const operations_research::RoutingIndexManager &index_manager);

        void AddDroppedVisitsHandling(operations_research::Solver *solver,
                                      operations_research::RoutingModel &model,
                                      const operations_research::RoutingIndexManager &index_manager);

        void AddDroppedVisitsHandling(operations_research::Solver *solver,
                                      operations_research::RoutingModel &model,
                                      const operations_research::RoutingIndexManager &index_manager,
                                      int64 penalty);

        void LimitDroppedVisits(operations_research::Solver *solver,
                                operations_research::RoutingModel &model,
                                const operations_research::RoutingIndexManager &index_manager,
                                int max_dropped_visits);

        void AddSkillHandling(operations_research::Solver *solver,
                              operations_research::RoutingModel &model,
                              const operations_research::RoutingIndexManager &index_manager);

        void AddContinuityOfCare(operations_research::Solver *solver,
                                 operations_research::RoutingModel &model,
                                 const operations_research::RoutingIndexManager &index_manager);

        std::vector<operations_research::IntervalVar *> CreateBreakIntervals(operations_research::Solver *solver,
                                                                             const rows::Carer &carer,
                                                                             const rows::Diary &diary) const;

        operations_research::IntervalVar *CreateBreakInterval(operations_research::Solver *solver,
                                                              const rows::Event &event,
                                                              std::string label) const;

        bool AddBreakIntervalVarIfNonZero(operations_research::Solver *solver,
                                          const rows::Event &event,
                                          std::string label,
                                          std::vector<operations_research::IntervalVar *> &intervals) const;

        operations_research::IntervalVar *CreateFixedInterval(operations_research::Solver *solver,
                                                              const rows::Event &event,
                                                              std::string label) const;

        bool AddFixedIntervalIfNonZero(operations_research::Solver *solver,
                                       const rows::Event &event,
                                       std::string label,
                                       std::vector<operations_research::IntervalVar *> &intervals) const;

        enum class BreakType {
            BREAK, BEFORE_WORKDAY, AFTER_WORKDAY
        };

        int64 GetBeginWindow(boost::posix_time::time_duration value,
                             boost::posix_time::time_duration window_size) const;

        int64 GetEndWindow(boost::posix_time::time_duration value,
                           boost::posix_time::time_duration window_size) const;

        int64 GetAdjustedWorkdayStart(boost::posix_time::time_duration start_time) const;

        int64 GetAdjustedWorkdayFinish(boost::posix_time::time_duration finish_time) const;

        rows::Solution Resolve(const rows::Solution &solution,
                               const std::vector<std::unique_ptr<rows::RouteValidatorBase::ValidationError> > &validation_errors) const;

        static std::string GetBreakLabel(const rows::Carer &carer, BreakType break_type);

        bool IsNear(const rows::CalendarVisit &left, const rows::CalendarVisit &right) const;

        const ProblemData &problem_data_;
        const LocalServiceUser depot_service_user_;

        bool out_office_hours_breaks_enabled_;

        boost::posix_time::time_duration visit_time_window_;
        boost::posix_time::time_duration break_time_window_;
        boost::posix_time::time_duration begin_end_work_day_adjustment_;

        operations_research::RoutingSearchParameters parameters_;
        std::unordered_map<rows::ServiceUser, rows::SolverWrapper::LocalServiceUser> service_users_;
    };
}


#endif //ROWS_SOLVER_WRAPPER_H
