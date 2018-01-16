#include "calendar_visit.h"

#include <boost/optional/optional_io.hpp>
#include <boost/format.hpp>

namespace rows {

    CalendarVisit::CalendarVisit(ServiceUser service_user,
                                 Address address,
                                 boost::optional<Location> location,
                                 boost::posix_time::ptime date_time,
                                 boost::posix_time::ptime::time_duration_type duration)
            : service_user_(std::move(service_user)),
              address_(std::move(address)),
              location_(std::move(location)),
              date_time_(date_time),
              duration_(std::move(duration)) {}

    CalendarVisit::CalendarVisit(ServiceUser service_user,
                                 Address address,
                                 boost::posix_time::ptime date_time,
                                 boost::posix_time::time_duration duration)
            : CalendarVisit(std::move(service_user),
                            std::move(address),
                            boost::optional<Location>(),
                            date_time,
                            std::move(duration)) {}

    CalendarVisit::CalendarVisit(const CalendarVisit &other)
            : service_user_(other.service_user_),
              address_(other.address_),
              location_(other.location_),
              date_time_(other.date_time_),
              duration_(other.duration_) {}

    CalendarVisit &CalendarVisit::operator=(const CalendarVisit &other) {
        service_user_ = other.service_user_;
        address_ = other.address_;
        location_ = other.location_;
        date_time_ = other.date_time_;
        duration_ = other.duration_;
        return *this;
    }

    std::ostream &operator<<(std::ostream &out, const CalendarVisit &object) {
        out << boost::format("(%1%, %2%, %3%, %4%, %5%)")
               % object.service_user_
               % object.address_
               % object.location_
               % object.date_time_
               % object.duration_;
        return out;
    }

    bool CalendarVisit::operator==(const CalendarVisit &other) const {
        return service_user_ == other.service_user_
               && address_ == other.address_
               && location_ == other.location_
               && date_time_ == other.date_time_
               && duration_ == other.duration_;
    }

    bool CalendarVisit::operator!=(const CalendarVisit &other) const {
        return !operator==(other);
    }

    const boost::optional<Location> &CalendarVisit::location() const {
        return location_;
    }

    void CalendarVisit::location(Location location) {
        location_ = std::move(location);
    }

    const boost::posix_time::ptime CalendarVisit::datetime() const {
        return date_time_;
    }

    const boost::posix_time::time_duration CalendarVisit::duration() const {
        return duration_;
    }
}