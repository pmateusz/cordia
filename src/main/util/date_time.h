#ifndef ROWS_DATE_TIME_H
#define ROWS_DATE_TIME_H

#include <boost/date_time.hpp>

namespace util {

    inline bool COMP_GT(const boost::posix_time::time_duration &left,
                        const boost::posix_time::time_duration &right,
                        const boost::posix_time::time_duration &margin) {
        return left > (right + margin);
    }

    inline bool COMP_LT(const boost::posix_time::time_duration &left,
                        const boost::posix_time::time_duration &right,
                        const boost::posix_time::time_duration &margin) {
        return (left + margin) < right;
    }

    inline bool COMP_NEAR(const boost::posix_time::time_duration &left,
                          const boost::posix_time::time_duration &right,
                          const boost::posix_time::time_duration &margin) {
        return !(COMP_GT(left, right, margin) && COMP_LT(left, right, margin));
    }
}


#endif //ROWS_DATE_TIME_H
