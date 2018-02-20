#ifndef ROWS_ROUTE_VALIDATOR_H
#define ROWS_ROUTE_VALIDATOR_H

#include <memory>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

#include <ortools/constraint_solver/routing.h>

#include <boost/date_time.hpp>
#include <boost/optional.hpp>

#include "calendar_visit.h"
#include "scheduled_visit.h"
#include "route.h"

namespace rows {

    class SolverWrapper;

    class Problem;

    class Location;

    class Carer;

    class Event;

    class Schedule {
    public:
        Schedule();

        Schedule(const Schedule &other);

        Schedule &operator=(const Schedule &other);

        struct Record {
            Record();

            Record(boost::posix_time::time_period arrival_interval,
                   boost::posix_time::time_duration travel_time,
                   ScheduledVisit visit);

            Record(const Record &other);

            Record &operator=(const Record &other);

            boost::posix_time::time_period ArrivalInterval;
            boost::posix_time::time_duration TravelTime;
            ScheduledVisit Visit;
        };

        boost::optional<Record> Find(const ScheduledVisit &visit) const;

        void Add(boost::posix_time::ptime arrival,
                 boost::posix_time::time_duration travel_time,
                 const ScheduledVisit &visit);

        const std::vector<Record> &records() const;

    private:
        std::vector<Record> records_;
    };

    class RouteValidatorBase {
    public:

        virtual ~RouteValidatorBase() = default;

        enum class ErrorCode {
            UNKNOWN,
            TOO_MANY_CARERS,
            ABSENT_CARER,
            BREAK_VIOLATION,
            LATE_ARRIVAL,
            MISSING_INFO,
            ORPHANED, // information about the visit is not available in the problem definition
            MOVED, // either start time or duration or both do not match the calendar visit
            NOT_ENOUGH_CARERS
        };

        class ValidationError {
        public:
            explicit ValidationError(RouteValidatorBase::ErrorCode error_code);

            ValidationError(RouteValidatorBase::ErrorCode error_code, std::string error_message);

            friend std::ostream &operator<<(std::ostream &out, const ValidationError &error);

            const std::string &error_message() const;

            RouteValidatorBase::ErrorCode error_code() const;

        protected:
            virtual void Print(std::ostream &out) const;

            RouteValidatorBase::ErrorCode error_code_;
            std::string error_message_;
        };

        class RouteConflictError : public ValidationError {
        public:
            RouteConflictError(const CalendarVisit &visit, std::vector<rows::Route> routes);

            const rows::CalendarVisit &visit() const;

            const std::vector<rows::Route> &routes() const;

        protected:
            virtual void Print(std::ostream &out) const;

        private:
            rows::CalendarVisit visit_;
            std::vector<rows::Route> routes_;
        };

        class ScheduledVisitError : public ValidationError {
        public:
            ScheduledVisitError(ErrorCode error_code,
                                std::string error_message,
                                const ScheduledVisit &visit,
                                const rows::Route &route);

            const rows::ScheduledVisit &visit() const;

        protected:
            virtual void Print(std::ostream &out) const;

        private:
            rows::ScheduledVisit visit_;
            rows::Route route_;
        };

        class Metrics {
        public:
            Metrics();

            Metrics(boost::posix_time::time_duration available_time,
                    boost::posix_time::time_duration service_time,
                    boost::posix_time::time_duration travel_time);

            Metrics(const Metrics &metrics);

            Metrics(Metrics &&metrics) noexcept;

            Metrics &operator=(const Metrics &metrics);

            Metrics &operator=(Metrics &&metrics) noexcept;

            boost::posix_time::time_duration available_time() const;

            boost::posix_time::time_duration service_time() const;

            boost::posix_time::time_duration travel_time() const;

            boost::posix_time::time_duration idle_time() const;

        private:
            boost::posix_time::time_duration available_time_;
            boost::posix_time::time_duration service_time_;
            boost::posix_time::time_duration travel_time_;
        };

        class ValidationResult {
        public:
            ValidationResult();

            ValidationResult(Metrics metrics, Schedule schedule);

            explicit ValidationResult(std::unique_ptr<ValidationError> &&error) noexcept;

            ValidationResult(const ValidationResult &other) = delete;

            ValidationResult(ValidationResult &&other) noexcept;

            ValidationResult &operator=(const ValidationResult &other) = delete;

            ValidationResult &operator=(ValidationResult &&other) noexcept;

            const Metrics &metrics() const;

            const Schedule &schedule() const;

            const std::unique_ptr<ValidationError> &error() const;

            std::unique_ptr<ValidationError> &error();

        private:
            Metrics metrics_;
            Schedule schedule_;
            std::unique_ptr<ValidationError> error_;
        };

        std::vector<std::unique_ptr<ValidationError> > ValidateAll(const std::vector<rows::Route> &routes,
                                                                   const rows::Problem &problem,
                                                                   rows::SolverWrapper &solver) const;

        ValidationResult Validate(const rows::Route &route, rows::SolverWrapper &solver) const;

        virtual ValidationResult Validate(const rows::Route &route,
                                          rows::SolverWrapper &solver,
                                          const std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> &earliest_arrival_times) const = 0;

    protected:
        static bool IsAssignedAndActive(const rows::ScheduledVisit &visit);
    };

    class ValidationSession {
    public:
        static const boost::posix_time::time_duration ERROR_MARGIN;

        ValidationSession(const Route &route, SolverWrapper &solver);

        void Initialize(
                const std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> &earliest_arrival_times);

        bool HasMoreVisits() const;

        bool HasMoreBreaks() const;

        const ScheduledVisit &GetCurrentVisit() const;

        const Event &GetCurrentBreak() const;

        void Perform(const ScheduledVisit &visit);

        void Perform(const Event &interval);

        boost::posix_time::time_duration GetBeginWindow(const Event &interval) const;

        boost::posix_time::time_duration GetBeginWindow(const ScheduledVisit &visit) const;

        boost::posix_time::time_duration GetEndWindow(const Event &interval) const;

        boost::posix_time::time_duration GetEndWindow(const ScheduledVisit &visit) const;

        boost::posix_time::time_duration GetExpectedFinish(const Event &interval) const;

        boost::posix_time::time_duration GetExpectedFinish(const ScheduledVisit &visit) const;

        boost::posix_time::time_duration GetTravelTime(operations_research::RoutingModel::NodeIndex from_node,
                                                       operations_research::RoutingModel::NodeIndex to_node) const;

        bool StartsAfter(boost::posix_time::time_duration time_of_day, const ScheduledVisit &visit) const;

        bool CanPerformAfter(boost::posix_time::time_duration time_of_day, const Event &break_interval) const;

        bool CanPerformAfter(boost::posix_time::time_duration time_of_day, const ScheduledVisit &visit) const;

        bool error() const;

        RouteValidatorBase::ValidationResult ToValidationResult();

        static bool GreaterThan(const boost::posix_time::time_duration &left,
                                const boost::posix_time::time_duration &right);

        static bool GreaterEqual(const boost::posix_time::time_duration &left,
                                 const boost::posix_time::time_duration &right);

        static RouteValidatorBase::ScheduledVisitError CreateMissingInformationError(const rows::Route &route,
                                                                                     const rows::ScheduledVisit &visit,
                                                                                     std::string error_msg);

        static RouteValidatorBase::ValidationError CreateValidationError(std::string error_msg);

        static RouteValidatorBase::ScheduledVisitError CreateAbsentCarerError(const rows::Route &route,
                                                                              const rows::ScheduledVisit &visit);

        static RouteValidatorBase::ScheduledVisitError CreateLateArrivalError(const rows::Route &route,
                                                                              const rows::ScheduledVisit &visit,
                                                                              boost::posix_time::ptime::time_duration_type duration);

        static RouteValidatorBase::ScheduledVisitError CreateContractualBreakViolationError(const rows::Route &route,
                                                                                            const rows::ScheduledVisit &visit);

        static RouteValidatorBase::ScheduledVisitError CreateContractualBreakViolationError(const rows::Route &route,
                                                                                            const rows::ScheduledVisit &visit,
                                                                                            std::vector<rows::Event> overlapping_slots);

        static RouteValidatorBase::ScheduledVisitError CreateOrphanedError(const Route &route,
                                                                           const ScheduledVisit &visit);

        static RouteValidatorBase::ScheduledVisitError NotEnoughCarersAvailable(const Route &route,
                                                                                const ScheduledVisit &visit);

        static RouteValidatorBase::ScheduledVisitError CreateMovedError(const Route &route,
                                                                        const ScheduledVisit &visit);

    private:
        operations_research::RoutingModel::NodeIndex GetNode(const ScheduledVisit &visit) const;

        const Route &route_;
        SolverWrapper &solver_;

        boost::gregorian::date date_;
        boost::posix_time::time_duration total_available_time_;
        boost::posix_time::time_duration total_service_time_;
        boost::posix_time::time_duration total_travel_time_;
        std::unique_ptr<RouteValidatorBase::ValidationError> error_;

        std::vector<ScheduledVisit> visits_;
        std::vector<operations_research::RoutingModel::NodeIndex> nodes_;
        operations_research::RoutingModel::NodeIndex last_node_;
        operations_research::RoutingModel::NodeIndex current_node_;
        operations_research::RoutingModel::NodeIndex next_node_;
        std::size_t current_visit_;

        std::vector<rows::Event> breaks_;
        std::size_t current_break_;
        boost::posix_time::time_duration current_time_;

        Schedule schedule_;
        std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> latest_arrival_times_;
    };

    class SolutionValidator {
    public:
        RouteValidatorBase::ValidationResult Validate(int vehicle,
                                                      const operations_research::Assignment &solution,
                                                      const operations_research::RoutingModel &model,
                                                      rows::SolverWrapper &solver) const;
    };

    class SimpleRouteValidatorWithTimeWindows : public RouteValidatorBase {
    public:
        friend class Session;

        virtual ~SimpleRouteValidatorWithTimeWindows() = default;

        using RouteValidatorBase::Validate;

        ValidationResult Validate(const rows::Route &route,
                                  rows::SolverWrapper &solver,
                                  const std::unordered_map<rows::CalendarVisit, boost::posix_time::time_duration> &latest_arrival_times) const override;
    };

    std::ostream &operator<<(std::ostream &out, RouteValidatorBase::ErrorCode error_code);
}


#endif //ROWS_ROUTE_VALIDATOR_H
