#include <glog/logging.h>

#include <boost/date_time.hpp>
#include <util/aplication_error.h>

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

    ScheduledVisit::ScheduledVisit(const ScheduledVisit &other)
            : type_(other.type_),
              carer_(other.carer_),
              datetime_(other.datetime_),
              duration_(other.duration_),
              check_in_(other.check_in_),
              check_out_(other.check_out_),
              calendar_visit_(other.calendar_visit_) {}

    ScheduledVisit::ScheduledVisit(ScheduledVisit &&other) noexcept
            : type_(other.type_),
              carer_(std::move(other.carer_)),
              datetime_(other.datetime_),
              duration_(other.duration_),
              check_in_(other.check_in_),
              check_out_(other.check_out_),
              calendar_visit_(std::move(other.calendar_visit_)) {}

    ScheduledVisit &ScheduledVisit::operator=(const ScheduledVisit &other) {
        type_ = other.type_;
        carer_ = other.carer_;
        datetime_ = other.datetime_;
        duration_ = other.duration_;
        check_in_ = other.check_in_;
        check_out_ = other.check_out_;
        calendar_visit_ = other.calendar_visit_;
        return *this;
    }

    ScheduledVisit &ScheduledVisit::operator=(ScheduledVisit &&other) noexcept {
        type_ = other.type_;
        carer_ = std::move(other.carer_);
        datetime_ = other.datetime_;
        duration_ = other.duration_;
        check_in_ = other.check_in_;
        check_out_ = other.check_out_;
        calendar_visit_ = std::move(other.calendar_visit_);
        return *this;
    }

    std::ostream &operator<<(std::ostream &out, const ScheduledVisit &visit) {
        out << "(" << visit.type_
            << ", " << visit.carer_
            << ", " << visit.check_in_
            << ", " << visit.check_out_
            << ", " << visit.datetime_
            << ", " << visit.duration_
            << ", " << visit.calendar_visit_
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

    void ScheduledVisit::type(VisitType type) {
        type_ = type;
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

    bool ScheduledVisit::operator==(const ScheduledVisit &other) const {
        return type_ == other.type_
               && carer_ == other.carer_
               && datetime_ == other.datetime_
               && duration_ == other.duration_
               && check_in_ == other.check_in_
               && check_out_ == other.check_out_
               && calendar_visit_ == other.calendar_visit_;
    }

    bool ScheduledVisit::operator!=(const ScheduledVisit &other) const {
        return !this->operator==(other);
    }

    const boost::optional<boost::posix_time::ptime> &ScheduledVisit::check_in() const {
        return check_in_;
    }

    const boost::optional<boost::posix_time::ptime> &ScheduledVisit::check_out() const {
        return check_out_;
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
            case ScheduledVisit::VisitType::INVALID:
                out << "INVALID";
                break;
            default:
                throw util::ApplicationError(
                        (boost::format("Handling not implemented for visit type: %1%") %
                         static_cast<int>(visit_type)).str(),
                        1);
        }
        return out;
    }
}
