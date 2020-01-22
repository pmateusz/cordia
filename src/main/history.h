#ifndef ROWS_HISTORY_H
#define ROWS_HISTORY_H

#include <vector>
#include <unordered_map>

#include "util/hash.h"
#include "past_visit.h"

namespace rows {

    class History {
    public:
        History();

        History(const std::vector<PastVisit> &past_visits);

        bool empty() const;

    private:
        std::unordered_map<long, std::unordered_map<boost::gregorian::date, std::vector<PastVisit> > > index_;
    };
}


#endif //ROWS_HISTORY_H
