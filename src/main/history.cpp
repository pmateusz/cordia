#include "history.h"

#include "util/hash.h"

rows::History::History()
        : History(std::vector<PastVisit>{}) {}

rows::History::History(const std::vector<PastVisit> &past_visits) {
    for (const auto &visit : past_visits) {
        const auto service_user = visit.service_user();
        const auto date = visit.date();

        auto service_user_it = index_.find(service_user);
        if (service_user_it == std::end(index_)) {
            std::unordered_map<boost::gregorian::date, std::vector<PastVisit> > service_user_visits;
            service_user_visits.emplace(date, std::vector<PastVisit>{visit});
            index_.emplace(service_user, std::move(service_user_visits));
        } else {
            auto date_it = service_user_it->second.find(date);
            if (date_it == std::end(service_user_it->second)) {
                service_user_it->second.emplace(date, std::vector<PastVisit>{visit});
            } else {
                date_it->second.emplace_back(visit);
            }
        }
    }
}

bool rows::History::empty() const {
    return index_.empty();
}
