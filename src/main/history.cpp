#include "history.h"

#include "util/hash.h"
#include "calendar_visit.h"

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

std::unordered_map<boost::gregorian::date, boost::posix_time::time_duration> rows::History::get_duration_sample(
        const rows::CalendarVisit &visit) const {
    static const boost::posix_time::hours MAX_START_TIME_DIFF = boost::posix_time::hours(2);

    std::unordered_map<boost::gregorian::date, boost::posix_time::time_duration> sample;

    const auto service_user_it = index_.find(visit.service_user().id());
    if (service_user_it == std::end(index_)) {
        return sample;
    }

    const auto visit_date = visit.datetime().date();
    for (const auto &date_visit_pair : service_user_it->second) {
        if (date_visit_pair.first >= visit_date) {
            continue;
        }

        for (const auto &past_visit : date_visit_pair.second) {
            auto start_time_diff = abs(past_visit.planned_check_in().time_of_day().total_seconds() - visit.datetime().time_of_day().total_seconds());
            if (start_time_diff > MAX_START_TIME_DIFF.total_seconds()) {
                continue;
            }

            if (past_visit.tasks() != visit.tasks()) {
                continue;
            }

            sample.emplace(past_visit.date(), past_visit.real_duration());
            break;
        }
    }

    return sample;
}
