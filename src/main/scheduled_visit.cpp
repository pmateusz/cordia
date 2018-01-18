#include "scheduled_visit.h"

#include <glog/logging.h>

namespace rows {

    ScheduledVisit::ScheduledVisit()
            : ScheduledVisit(ScheduledVisit::VisitType::UNKNOWN, {}, {}, {}, {}, {}, {}) {}

    ScheduledVisit::ScheduledVisit(ScheduledVisit::VisitType type,
                                   boost::optional<Carer> carer,
                                   boost::posix_time::ptime datetime,
                                   boost::posix_time::ptime::time_duration_type duration,
                                   boost::optional<boost::posix_time::ptime> check_in,
                                   boost::optional<boost::posix_time::ptime> check_out,
                                   boost::optional<CalendarVisit> calendar_visit)
            : type_(type),
              carer_(carer),
              datetime_(datetime),
              duration_(duration),
              check_in_(check_in),
              check_out_(check_out),
              calendar_visit_(calendar_visit) {}

    std::ostream &operator<<(std::ostream &out, const ScheduledVisit &visit) {
        out << "(" << visit.type_
            << ", " << visit.carer_
            << ", " << visit.calendar_visit_
            << ", " << visit.carer_
            << ", " << visit.check_in_
            << ", " << visit.check_out_
            << ", " << visit.datetime_
            << ", " << visit.duration_
            << ")";
        return out;
    }

    const boost::posix_time::ptime &ScheduledVisit::datetime() const {
        return datetime_;
    }

    const boost::optional<Carer> &ScheduledVisit::carer() const {
        return carer_;
    }

    boost::optional<Carer> &ScheduledVisit::carer() {
        return carer_;
    }

    ScheduledVisit::VisitType ScheduledVisit::type() const {
        return type_;
    }

    const boost::optional<CalendarVisit> &ScheduledVisit::calendar_visit() const {
        return calendar_visit_;
    }

    const boost::posix_time::ptime::time_duration_type &ScheduledVisit::duration() const {
        return duration_;
    }

    const boost::optional<Location> ScheduledVisit::location() const {
        if (!calendar_visit_.is_initialized()) {
            return boost::optional<Location>();
        }

        return calendar_visit_.get().location();
    }

    void ScheduledVisit::location(const rows::Location &location) {
        DCHECK(calendar_visit_);

        calendar_visit_.get().location(location);
    }

    boost::optional<ServiceUser> ScheduledVisit::service_user() const {
        if (!calendar_visit_.is_initialized()) {
            return boost::optional<ServiceUser>();
        }

        return boost::make_optional(calendar_visit_.get().service_user());
    }

    std::ostream &operator<<(std::ostream &out, const ScheduledVisit::VisitType &visit_type) {
        switch (visit_type) {
            case ScheduledVisit::VisitType::UNKNOWN:
                out << "UNKNOWN";
                break;
            case ScheduledVisit::VisitType::CANCELLED:
                out << "CANCELLED";
                break;
            case ScheduledVisit::VisitType::MOVED:
                out << "MOVED";
                break;
            case ScheduledVisit::VisitType::OK:
                out << "OK";
                break;
        }
        return out;
    }
}
