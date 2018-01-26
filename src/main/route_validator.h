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

    class RouteValidator {
    public:

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
            ValidationError(ErrorCode error_code);

            friend std::ostream &operator<<(std::ostream &out, const ValidationError &error);

            ErrorCode error_code() const;

        protected:
            virtual void Print(std::ostream &out) const = 0;

            ErrorCode error_code_;
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
                                const ScheduledVisit &visit,
                                const rows::Route &route,
                                std::string error_message);

            const rows::ScheduledVisit &visit() const;

        protected:
            virtual void Print(std::ostream &out) const;

        private:
            rows::ScheduledVisit visit_;
            rows::Route route_;
            std::string error_message_;
        };

        class ValidationResult {
        public:
            ValidationResult();

            ValidationResult(boost::posix_time::time_duration available_time,
                             boost::posix_time::time_duration service_time,
                             boost::posix_time::time_duration travel_time);

            ValidationResult(const ValidationResult &other) = delete;

            ValidationResult(ValidationResult &&other) noexcept;

            ValidationResult &operator=(const ValidationResult &other) = delete;

            ValidationResult &operator=(ValidationResult &&other) noexcept;

            ValidationResult(std::unique_ptr<ValidationError> &&error) noexcept;

            const std::unique_ptr<ValidationError> &error() const;

            std::unique_ptr<ValidationError> &error();

        private:
            boost::posix_time::time_duration available_time_;
            boost::posix_time::time_duration service_time_;
            boost::posix_time::time_duration travel_time_;
            std::unique_ptr<ValidationError> error_;
        };

        std::vector<std::unique_ptr<ValidationError> > Validate(const std::vector<rows::Route> &routes,
                                                                const rows::Problem &problem,
                                                                rows::SolverWrapper &solver) const;

        ValidationResult Validate(const rows::Route &route,
                                  const rows::Problem &problem,
                                  rows::SolverWrapper &solver) const;

    private:
        static bool IsAssignedAndActive(const rows::ScheduledVisit &visit);

        ScheduledVisitError CreateMissingInformationError(const rows::Route &route,
                                                          const rows::ScheduledVisit &visit,
                                                          std::string error_msg) const;

        ScheduledVisitError CreateAbsentCarerError(const rows::Route &route,
                                                   const rows::ScheduledVisit &visit) const;

        ScheduledVisitError CreateLateArrivalError(const rows::Route &route,
                                                   const rows::ScheduledVisit &visit,
                                                   const boost::posix_time::ptime::time_duration_type duration) const;

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

    std::ostream &operator<<(std::ostream &out, RouteValidator::ErrorCode error_code);
}


#endif //ROWS_ROUTE_VALIDATOR_H
