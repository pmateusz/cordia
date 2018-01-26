#include "utility"

#include "route.h"


namespace rows {

    Route::Route()
            : Route(Carer(), std::vector<ScheduledVisit>()) {}

    Route::Route(Carer carer)
            : Route(std::move(carer), std::vector<rows::ScheduledVisit>()) {}

    Route::Route(rows::Carer carer, std::vector<rows::ScheduledVisit> visits)
            : carer_(std::move(carer)),
              visits_(std::move(visits)) {}

    Route::Route(const rows::Route &route)
            : carer_(route.carer_),
              visits_(route.visits_) {}

    Route::Route(rows::Route &&route) noexcept
            : carer_(std::move(route.carer_)),
              visits_(std::move(route.visits_)) {}

    Route &Route::operator=(const rows::Route &route) {
        carer_ = route.carer_;
        visits_ = route.visits_;
        return *this;
    }

    Route &Route::operator=(rows::Route &&route) noexcept {
        carer_ = std::move(route.carer_);
        visits_ = std::move(route.visits_);
        return *this;
    }

    const Carer &Route::carer() const {
        return carer_;
    }

    const std::vector<ScheduledVisit> &rows::Route::visits() const {
        return visits_;
    }

    std::vector<ScheduledVisit> &Route::visits() {
        return visits_;
    }
}
