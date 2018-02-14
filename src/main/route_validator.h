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
            MOVED // either start time or duration or both do not match the calendar visit
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

            explicit ValidationResult(Metrics metrics);

            explicit ValidationResult(std::unique_ptr<ValidationError> &&error) noexcept;

            ValidationResult(const ValidationResult &other) = delete;

            ValidationResult(ValidationResult &&other) noexcept;

            ValidationResult &operator=(const ValidationResult &other) = delete;

            ValidationResult &operator=(ValidationResult &&other) noexcept;

            const Metrics &metrics() const;

            const std::unique_ptr<ValidationError> &error() const;

            std::unique_ptr<ValidationError> &error();

        private:
            Metrics metrics_;
            std::unique_ptr<ValidationError> error_;
        };

        std::vector<std::unique_ptr<ValidationError> > ValidateAll(const std::vector<rows::Route> &routes,
                                                                const rows::Problem &problem,
                                                                rows::SolverWrapper &solver) const;

        virtual ValidationResult Validate(const rows::Route &route, rows::SolverWrapper &solver) const = 0;

    protected:
        static bool IsAssignedAndActive(const rows::ScheduledVisit &visit);

        ScheduledVisitError CreateMissingInformationError(const rows::Route &route,
                                                          const rows::ScheduledVisit &visit,
                                                          std::string error_msg) const;

        ValidationError CreateValidationError(std::string error_msg) const;

        ScheduledVisitError CreateAbsentCarerError(const rows::Route &route,
                                                   const rows::ScheduledVisit &visit) const;

        ScheduledVisitError CreateLateArrivalError(const rows::Route &route,
                                                   const rows::ScheduledVisit &visit,
                                                   boost::posix_time::ptime::time_duration_type duration) const;

        ScheduledVisitError CreateContractualBreakViolationError(const rows::Route &route,
                                                                 const rows::ScheduledVisit &visit) const;

        ScheduledVisitError CreateContractualBreakViolationError(const rows::Route &route,
                                                                 const rows::ScheduledVisit &visit,
                                                                 std::vector<rows::Event> overlapping_slots) const;

        ScheduledVisitError CreateOrphanedError(const Route &route,
                                                const ScheduledVisit &visit) const;

        ScheduledVisitError CreateMovedError(const Route &route,
                                             const ScheduledVisit &visit) const;
    };

    class RouteValidator : public RouteValidatorBase {
    public:
        virtual ~RouteValidator() = default;

        ValidationResult Validate(const rows::Route &route, rows::SolverWrapper &solver) const override;
    };

    class SimpleRouteValidator : public RouteValidatorBase {
    public:
        virtual ~SimpleRouteValidator() = default;

        ValidationResult Validate(const rows::Route &route, rows::SolverWrapper &solver) const override;
    };

    std::ostream &operator<<(std::ostream &out, RouteValidatorBase::ErrorCode error_code);
}


#endif //ROWS_ROUTE_VALIDATOR_H
