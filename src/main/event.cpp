#include "event.h"

#include <boost/format.hpp>

namespace rows {

    Event::Event(const boost::posix_time::ptime &begin, const boost::posix_time::ptime &end)
            : begin_(begin),
              end_(end) {}

    const boost::posix_time::ptime &Event::begin() const {
        return begin_;
    }

    const boost::posix_time::ptime &Event::end() const {
        return end_;
    }

    std::ostream &operator<<(std::ostream &out, const Event &object) {
        out << boost::format("(%1%,%2%)")
               % object.begin_
               % object.end_;
        return out;
    }

    bool Event::operator==(const Event &other) const {
        return begin_ == other.begin_
               && end_ == other.end_;
    }

    bool Event::operator!=(const Event &other) const {
        return !operator==(other);
    }
}