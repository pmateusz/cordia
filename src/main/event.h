#ifndef ROWS_EVENT_H
#define ROWS_EVENT_H

#include <ostream>
#include <utility>

#include <boost/date_time/posix_time/posix_time.hpp>

namespace rows {

    // TODO: switch to time period
    class Event {
    public:
        Event(const boost::posix_time::ptime &begin, const boost::posix_time::ptime &end);

        Event(const Event &other) = default;

        Event &operator=(const Event &other) = default;

        bool operator==(const Event &other) const;

        bool operator!=(const Event &other) const;

        const boost::posix_time::ptime &begin() const;

        const boost::posix_time::ptime &end() const;

        friend std::ostream &operator<<(std::ostream &out, const Event &object);

    private:
        boost::posix_time::ptime begin_;
        boost::posix_time::ptime end_;
    };
}


#endif //ROWS_EVENT_H
