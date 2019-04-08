#ifndef ROWS_BREAK_H
#define ROWS_BREAK_H

#include <iostream>

#include <boost/config.hpp>
#include <boost/date_time.hpp>

#include "carer.h"

namespace rows {

    class Break {
    public:
        Break(Carer carer, boost::posix_time::ptime datetime, boost::posix_time::ptime::time_duration_type duration);

        inline const Carer &carer() const { return carer_; }

        inline const boost::posix_time::ptime &datetime() const { return datetime_; }

        inline const boost::posix_time::ptime::time_duration_type &duration() const { return duration_; }

    private:
        Carer carer_;
        boost::posix_time::ptime datetime_;
        boost::posix_time::ptime::time_duration_type duration_;
    };
}

std::ostream &operator<<(std::ostream &out, const rows::Break &break_element);


#endif //ROWS_BREAK_H
