#ifndef ROWS_DATE_TIME_H
#define ROWS_DATE_TIME_H

#include <boost/date_time.hpp>

namespace util {

    template<typename TimeAwareType>
    inline bool COMP_GT(TimeAwareType left,
                        TimeAwareType right,
                        const boost::posix_time::time_duration &margin) {
        return left > (right + margin);
    }

    template<typename TimeAwareType>
    inline bool COMP_LT(TimeAwareType left,
                        TimeAwareType right,
                        const boost::posix_time::time_duration &margin) {
        return (left + margin) < right;
    }

    template<typename TimeAwareType>
    inline bool COMP_NEAR(TimeAwareType left,
                          TimeAwareType right,
                          const boost::posix_time::time_duration &margin) {
        return !(COMP_GT(left, right, margin) && COMP_LT(left, right, margin));
    }

    const boost::posix_time::time_duration ERROR_MARGIN = boost::posix_time::seconds(1);

    template<typename TimeAwareType>
    inline bool COMP_GT(TimeAwareType left, TimeAwareType right) {
        return util::COMP_GT(left, right, ERROR_MARGIN);
    }

    template<typename TimeAwareType>
    inline bool COMP_GE(TimeAwareType left, TimeAwareType right) {
        return util::COMP_GT(left, right, ERROR_MARGIN) || util::COMP_NEAR(left, right, ERROR_MARGIN);
        //(abs(left.total_seconds() - right.total_seconds()) <= ERROR_MARGIN.total_seconds());
    }
}


#endif //ROWS_DATE_TIME_H
