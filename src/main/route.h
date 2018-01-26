#ifndef ROWS_ROUTE_H
#define ROWS_ROUTE_H

#include <vector>

#include "carer.h"
#include "scheduled_visit.h"

namespace rows {

    class Route {
    public:
        Route();

        explicit Route(Carer carer);

        Route(Carer carer, std::vector<ScheduledVisit> visits);

        Route(const Route &route);

        Route(Route &&route) noexcept;

        Route &operator=(const Route &route);

        Route &operator=(Route &&route) noexcept;

        const Carer &carer() const;

        const std::vector<ScheduledVisit> &visits() const;

        std::vector<ScheduledVisit> &visits();

    private:
        Carer carer_;
        std::vector<ScheduledVisit> visits_;
    };
}


#endif //ROWS_ROUTE_H
