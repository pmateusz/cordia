#include "event.h"

namespace rows {

    Event::Event(const boost::posix_time::time_period &period)
            : period_(period) {}

    Event::Event(const Event &other)
            : period_(other.period_) {}

    Event::Event(Event &&other) noexcept
            : period_(other.period_) {}

    Event &Event::operator=(const Event &other) {
        period_ = other.period_;
        return *this;
    }

    Event &Event::operator=(Event &&other) noexcept {
        period_ = other.period_;
        return *this;
    }

    bool Event::operator==(const Event &other) const {
        return period_ == other.period_;
    }

    bool Event::operator!=(const Event &other) const {
        return !operator==(other);
    }

    boost::posix_time::ptime Event::begin() const {
        return period_.begin();
    }

    boost::posix_time::ptime Event::end() const {
        return period_.end();
    }

    std::ostream &operator<<(std::ostream &out, const Event &object) {
        out << object.period_;
        return out;
    }

    boost::posix_time::time_duration Event::duration() const {
        return period_.length();
    }
}