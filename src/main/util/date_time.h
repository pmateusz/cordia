#ifndef ROWS_DATE_TIME_H
#define ROWS_DATE_TIME_H

#include <boost/date_time.hpp>

namespace util {

    inline bool COMP_GT(boost::posix_time::time_duration left,
                        boost::posix_time::time_duration right,
                        boost::posix_time::time_duration margin) {
        return left > right;
    }

    inline bool COMP_LT(boost::posix_time::time_duration left,
                        boost::posix_time::time_duration right,
                        boost::posix_time::time_duration margin) {
        return left < right;
    }

    inline bool COMP_NEAR(boost::posix_time::time_duration left,
                          boost::posix_time::time_duration right,
                          boost::posix_time::time_duration margin) {
        return !(COMP_GT(left, right, margin) && COMP_LT(left, right, margin));
    }
}


#endif //ROWS_DATE_TIME_H
