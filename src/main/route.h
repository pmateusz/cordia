#ifndef ROWS_ROUTE_H
#define ROWS_ROUTE_H

#include <vector>

#include "carer.h"
#include "scheduled_visit.h"

namespace rows {

    class Route {
    public:
        Route(const Carer &carer, std::vector<ScheduledVisit> visits);

        const Carer &carer() const;

        const std::vector<ScheduledVisit> &visits() const;

    private:
        Carer carer_;
        std::vector<ScheduledVisit> visits_;
    };
}


#endif //ROWS_ROUTE_H
