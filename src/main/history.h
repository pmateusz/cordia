#ifndef ROWS_HISTORY_H
#define ROWS_HISTORY_H

#include <vector>
#include <unordered_map>

#include <boost/date_time.hpp>

#include "util/hash.h"
#include "past_visit.h"

namespace rows {

    class CalendarVisit;

    class History {
    public:
        History();

        History(const std::vector<PastVisit> &past_visits);

        bool empty() const;

        std::unordered_map<boost::gregorian::date, boost::posix_time::time_duration> get_duration_sample(const CalendarVisit &visit) const;

    private:
        std::unordered_map<long, std::unordered_map<boost::gregorian::date, std::vector<PastVisit> > > index_;
    };
}


#endif //ROWS_HISTORY_H
