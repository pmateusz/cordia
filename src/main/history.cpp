#include "history.h"

#include "util/hash.h"
#include "calendar_visit.h"

rows::History::History()
        : History(std::vector<PastVisit>{}) {}

rows::History::History(const std::vector<PastVisit> &past_visits) {
    bool visit_added = false;

    for (const auto &visit : past_visits) {
        const auto service_user = visit.service_user();
        const auto date = visit.date();

        if (visit.service_user() == 9087917 && date == boost::gregorian::date(2017, 3, 11)) {
            visit_added = true;
        }

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

        if (visit_added) {
            const auto service_user_it = index_.find(9087917);
            const auto visit_it = service_user_it->second.find(boost::gregorian::date(2017, 3, 11));
            CHECK(visit_it != std::cend(service_user_it->second));
        }
    }

    LOG(INFO) << "HERE";
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
//    for (const auto &date_visit_pair : service_user_it->second) {
//        if (date_visit_pair.first >= visit_date) {
//            continue;
//        }
//
//        if (visit.id() == 8533569) {
//            for (const auto &local_visit : date_visit_pair.second) {
//                std::stringstream msg;
//
//                msg << std::endl;
//                msg << "visit = " << local_visit.id() << std::endl;
//                msg << "service_user = " << local_visit.service_user() << std::endl;
//                msg << "planned_check_in = " << local_visit.planned_check_in() << std::endl;
//                msg << "real_duration = " << local_visit.real_duration() << " " << local_visit.real_duration().total_seconds() << std::endl;
//
//                LOG(INFO) << msg.str();
//            }
//        }
//    }

//    if (visit.id() == 8533569) {
//        const auto visits = service_user_it->second.find(boost::gregorian::date(2017, 3, 11));
//        for (const auto &visit : visits->second) {
//            LOG(INFO) << visit.date() << " " << visit.planned_check_in();
//        }
//        LOG(INFO) << "VISITS";
//    }

    for (const auto &date_visit_pair : service_user_it->second) {
        if (date_visit_pair.first >= visit_date) { continue; }

        for (const auto &past_visit : date_visit_pair.second) {
            if (visit.id() == 8533606 && past_visit.date() == boost::gregorian::date(2017, 2, 25)) {
                LOG(INFO) << visit.datetime();
                LOG(INFO) << past_visit.planned_check_in();
            }

            auto start_time_diff = abs(past_visit.planned_check_in().time_of_day().total_seconds() - visit.datetime().time_of_day().total_seconds());
            if (start_time_diff > MAX_START_TIME_DIFF.total_seconds()) {
                continue;
            }

            if (past_visit.tasks() != visit.tasks()) {
                continue;
            }

            auto find_it = sample.find(past_visit.date());
            if (find_it != std::cend(sample)) {
                const auto past_visit_duration = past_visit.real_duration();
                const auto current_visit_duration = find_it->second;
                const auto total_seconds = (current_visit_duration.total_seconds() + past_visit_duration.total_seconds()) / 2;

//                if (visit.id() == 8533569 && past_visit.date() == boost::gregorian::date(2017, 3, 11)) {
//                    LOG(INFO) << boost::posix_time::seconds(total_seconds);
//                }

                sample[past_visit.date()] = boost::posix_time::seconds(total_seconds);
            } else {
//                if (visit.id() == 8533569 && past_visit.date() == boost::gregorian::date(2017, 3, 11)) {
//                    LOG(INFO) << past_visit.real_duration();
//                }

                sample.emplace(past_visit.date(), past_visit.real_duration());
            }
            break;
        }
    }

    return sample;
}
