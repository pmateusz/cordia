#include "visit.h"

#include <boost/format.hpp>

namespace rows {

    Visit::Visit(rows::Location location,
                 boost::gregorian::date date,
                 boost::posix_time::time_duration time,
                 boost::posix_time::time_duration duration)
            : location_(location),
              date_(date),
              time_(time),
              duration_(duration) {}

    Visit::Visit(const Visit &other)
            : location_(other.location_),
              date_(other.date_),
              time_(other.time_),
              duration_(other.duration_) {}

    Visit &Visit::operator=(const Visit &other) {
        location_ = other.location_;
        date_ = other.date_;
        time_ = other.time_;
        duration_ = other.duration_;
        return *this;
    }

    std::ostream &operator<<(std::ostream &out, const Visit &object) {
        out << boost::format("(%1%, %2%, %3%, %4%)")
               % object.location_
               % object.date_
               % object.time_
               % object.duration_;
        return out;
    }

    bool Visit::operator==(const Visit &other) const {
        return location_ == other.location_
               && date_ == other.date_
               && time_ == other.time_
               && duration_ == other.duration_;
    }

    bool Visit::operator!=(const Visit &other) const {
        return !operator==(other);
    }

    const Location &Visit::location() const {
        return location_;
    }

    const boost::gregorian::date Visit::date() const {
        return date_;
    }

    const boost::posix_time::time_duration Visit::time() const {
        return time_;
    }

    const boost::posix_time::time_duration Visit::duration() const {
        return duration_;
    }
}
