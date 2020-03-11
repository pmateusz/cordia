#include "calendar_visit.h"

#include <boost/optional.hpp>
#include <boost/format.hpp>
#include <boost/date_time.hpp>

#include <glog/logging.h>

#include "util/json.h"

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

    CalendarVisit::CalendarVisit(std::size_t id,
                                 ServiceUser service_user,
                                 Address address,
                                 boost::optional<Location> location,
                                 boost::posix_time::time_period time_windows,
                                 boost::posix_time::time_duration duration,
                                 int carer_count,
                                 std::vector<int> tasks)
            : id_(id),
              service_user_(std::move(service_user)),
              address_(std::move(address)),
              location_(std::move(location)),
              time_windows_(time_windows),
              duration_(std::move(duration)),
              carer_count_(carer_count),
              tasks_(std::move(tasks)) {}

    CalendarVisit::CalendarVisit(std::size_t id,
                                 ServiceUser service_user,
                                 Address address,
                                 boost::optional<Location> location,
                                 boost::posix_time::ptime date_time,
                                 boost::posix_time::ptime::time_duration_type duration,
                                 int carer_count,
                                 std::vector<int> tasks)
            : CalendarVisit{id,
                            service_user,
                            address,
                            location,
                            boost::posix_time::time_period{date_time, date_time},
                            duration,
                            carer_count,
                            tasks} {}

    std::ostream &operator<<(std::ostream &out, const CalendarVisit &object) {
        out << boost::format("(%1% %2%, %3%, %4%, %5%, %6%, %7%)")
               % object.id_
               % object.service_user_
               % object.address_
               % object.location_
               % object.time_windows_
               % object.duration_
               % object.carer_count_;
        return out;
    }

    bool CalendarVisit::operator==(const CalendarVisit &other) const {
        return id_ == other.id_
               && service_user_ == other.service_user_
               && address_ == other.address_
               && time_windows_ == other.time_windows_
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
        return time_windows_.begin() + time_windows_.length() / 2;
    }

    const boost::posix_time::time_duration &CalendarVisit::duration() const {
        return duration_;
    }

    const boost::posix_time::time_period &CalendarVisit::time_windows() const {
        return time_windows_;
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
        time_windows_ = boost::posix_time::time_period{date_time, date_time};
    }

    void CalendarVisit::duration(const boost::posix_time::ptime::time_duration_type &duration) {
        duration_ = duration;
    }

    void from_json(const nlohmann::json &json, CalendarVisit &visit) {
        const auto key = json.at("key").get<std::size_t>();
        const auto service_user = json.at("service_user").get<std::string>();
        const auto carer_count = json.at("carer_count").get<int>();
        const auto duration = json.at("duration").get<boost::posix_time::time_duration>();
        const auto date = json.at("date").get<boost::gregorian::date>();
        const auto time_of_day = json.at("time").get<boost::posix_time::time_duration>();
        std::vector<int> tasks;

        CalendarVisit output_visit{key,
                                   rows::ServiceUser(std::stol(service_user)),
                                   Address{},
                                   boost::posix_time::ptime{date, time_of_day},
                                   duration,
                                   carer_count,
                                   tasks};

        visit = output_visit;
    }
}
