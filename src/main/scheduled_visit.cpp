#include "scheduled_visit.h"

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
