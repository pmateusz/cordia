#ifndef ROWS_EVENT_H
#define ROWS_EVENT_H

#include <ostream>
#include <utility>

#include <boost/date_time/posix_time/posix_time.hpp>

namespace rows {

    class Event {
    public:
        explicit Event(const boost::posix_time::time_period &period);

        Event(const Event &other);

        Event(Event &&other) noexcept;

        Event &operator=(const Event &other);

        Event &operator=(Event &&other) noexcept;

        bool operator==(const Event &other) const;

        bool operator!=(const Event &other) const;

        boost::posix_time::ptime begin() const;

        boost::posix_time::ptime end() const;

        boost::posix_time::time_duration duration() const;

        friend std::ostream &operator<<(std::ostream &out, const Event &object);

    private:
        boost::posix_time::time_period period_;
    };
}


#endif //ROWS_EVENT_H
