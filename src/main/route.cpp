#include "route.h"

namespace rows {

    Route::Route(const rows::Carer &carer, std::vector<rows::ScheduledVisit> visits)
            : carer_(carer),
              visits_(std::move(visits)) {}

    const Carer &Route::carer() const {
        return carer_;
    }

    const std::vector<ScheduledVisit> &Route::visits() const {
        return visits_;
    }
}
