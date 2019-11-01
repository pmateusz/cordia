#include "calendar_visit.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/optional.hpp>
#include <boost/format.hpp>

#include <glog/logging.h>

namespace rows {

    CalendarVisit::CalendarVisit()
            : CalendarVisit(0,
                            ServiceUser(),
                            Address(),
                            boost::none,
                            boost::posix_time::not_a_date_time,
                            boost::posix_time::seconds(0),
                            0,
                            std::vector<int>()) {}

    CalendarVisit::CalendarVisit(std::size_t id,
                                 ServiceUser service_user,
                                 Address address,
                                 boost::optional<Location> location,
                                 boost::posix_time::ptime date_time,
                                 boost::posix_time::ptime::time_duration_type duration,
                                 int carer_count,
                                 std::vector<int> tasks)
            : id_(id),
              service_user_(std::move(service_user)),
              address_(std::move(address)),
              location_(std::move(location)),
              date_time_(date_time),
              duration_(std::move(duration)),
              carer_count_(carer_count),
              tasks_(std::move(tasks)) {}

    CalendarVisit::CalendarVisit(std::size_t id,
                                 ServiceUser service_user,
                                 Address address,
                                 boost::posix_time::ptime date_time,
                                 boost::posix_time::time_duration duration,
                                 int carer_count,
                                 std::vector<int> tasks)
            : CalendarVisit(id,
                            std::move(service_user),
                            std::move(address),
                            boost::none,
                            date_time,
                            std::move(duration),
                            carer_count,
                            std::move(tasks)) {}

    CalendarVisit::CalendarVisit(const CalendarVisit &other)
            : id_(other.id_),
              service_user_(other.service_user_),
              address_(other.address_),
              location_(other.location_),
              date_time_(other.date_time_),
              duration_(other.duration_),
              carer_count_(other.carer_count_),
              tasks_(other.tasks_) {}

    CalendarVisit::CalendarVisit(CalendarVisit &&other) noexcept
            : id_(other.id_),
              service_user_(std::move(other.service_user_)),
              address_(std::move(other.address_)),
              location_(other.location_),
              date_time_(other.date_time_),
              duration_(other.duration_),
              carer_count_(other.carer_count_),
              tasks_{std::move(other.tasks_)} {}

    CalendarVisit &CalendarVisit::operator=(const CalendarVisit &other) {
        id_ = other.id_;
        service_user_ = other.service_user_;
        address_ = other.address_;
        location_ = other.location_;
        date_time_ = other.date_time_;
        duration_ = other.duration_;
        carer_count_ = other.carer_count_;
        tasks_ = other.tasks_;
        return *this;
    }

    CalendarVisit &CalendarVisit::operator=(CalendarVisit &&other) noexcept {
        id_ = other.id_;
        service_user_ = std::move(other.service_user_);
        address_ = std::move(other.address_);
        location_ = other.location_;
        date_time_ = other.date_time_;
        duration_ = other.duration_;
        carer_count_ = other.carer_count_;
        tasks_ = other.tasks_;
        return *this;
    }

    std::ostream &operator<<(std::ostream &out, const CalendarVisit &object) {
        out << boost::format("(%1% %2%, %3%, %4%, %5%, %6%, %7%)")
               % object.id_
               % object.service_user_
               % object.address_
               % object.location_
               % object.date_time_
               % object.duration_
               % object.carer_count_;
        return out;
    }

    bool CalendarVisit::operator==(const CalendarVisit &other) const {
        return id_ == other.id_
               && service_user_ == other.service_user_
               && address_ == other.address_
               && date_time_ == other.date_time_
               && duration_ == other.duration_
               && location_ == other.location_
               && carer_count_ == other.carer_count_
               && tasks_ == other.tasks_;
    }

    bool CalendarVisit::operator!=(const CalendarVisit &other) const {
        return !operator==(other);
    }

    std::size_t CalendarVisit::id() const {
        return id_;
    }

    const boost::optional<Location> &CalendarVisit::location() const {
        return location_;
    }

    void CalendarVisit::location(Location location) {
        location_ = std::move(location);
    }

    const Address &CalendarVisit::address() const {
        return address_;
    }

    void CalendarVisit::address(Address address) {
        address_ = address;
    }

    const boost::posix_time::ptime CalendarVisit::datetime() const {
        return date_time_;
    }

    const boost::posix_time::time_duration CalendarVisit::duration() const {
        return duration_;
    }

    const ServiceUser &CalendarVisit::service_user() const {
        return service_user_;
    }

    ServiceUser &CalendarVisit::service_user() {
        return service_user_;
    }

    int CalendarVisit::carer_count() const {
        return carer_count_;
    }

    void CalendarVisit::carer_count(int value) {
        carer_count_ = value;
    }

    const std::vector<int> &CalendarVisit::tasks() const {
        return tasks_;
    }

    void CalendarVisit::datetime(const boost::posix_time::ptime &date_time) {
        date_time_ = date_time;
    }

    void CalendarVisit::duration(const boost::posix_time::ptime::time_duration_type &duration) {
        duration_ = duration;
    }
}
