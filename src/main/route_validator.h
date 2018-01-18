#ifndef ROWS_ROUTE_VALIDATOR_H
#define ROWS_ROUTE_VALIDATOR_H

#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "route.h"
#include "problem.h"
#include "solver_wrapper.h"

namespace rows {

    class RouteValidator {
    public:

        enum class ErrorCode {
            UNKNOWN,
            TOO_MANY_CARERS,
            ABSENT_CARER,
            BREAK_VIOLATION,
            LATE_ARRIVAL,
            MISSING_INFO
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

        std::vector<std::unique_ptr<ValidationError> > Validate(const std::vector<rows::Route> &routes,
                                                                const rows::Problem &problem,
                                                                rows::SolverWrapper &solver) const;

    private:
        static bool IsAssignedAndActive(const rows::ScheduledVisit &visit);
    };

    std::ostream &operator<<(std::ostream &out, RouteValidator::ErrorCode error_code);
}


#endif //ROWS_ROUTE_VALIDATOR_H
