#include "problem.h"

#include <boost/format.hpp>
#include <boost/date_time.hpp>

#include <glog/logging.h>

namespace rows {

    Problem::Problem(std::vector<CalendarVisit> visits, std::vector<std::pair<Carer, std::vector<Diary> > > carers)
            : visits_(std::move(visits)),
              carers_(std::move(carers)) {}

    const std::vector<CalendarVisit> &Problem::visits() const {
        return visits_;
    }

    const std::vector<std::pair<Carer, std::vector<Diary> > > &Problem::carers() const {
        return carers_;
    }

    bool Problem::IsAdmissible() const {
        auto required_duration_sec = 0;
        for (const auto &visit : visits_) {
            required_duration_sec += visit.duration().total_seconds();
        }

        auto available_duration_sec = 0;
        for (const auto &carer_diary_pair : carers_) {
            for (const auto &diary : carer_diary_pair.second) {
                for (const auto &event : diary.events()) {
                    DCHECK_LE(event.begin(), event.end());

                    available_duration_sec += (event.end() - event.begin()).total_seconds();
                }
            }
        }

        return required_duration_sec <= available_duration_sec;
    }

    std::pair<boost::posix_time::ptime, boost::posix_time::ptime> Problem::Timespan() const {
        LOG_ASSERT(!visits_.empty());

        std::vector<boost::posix_time::ptime> times;
        for (const auto &visit : visits_) {
            times.push_back(visit.datetime());
        }

        const auto pair_it = std::minmax_element(std::cbegin(times), std::cend(times));
        return std::make_pair(*pair_it.first, *pair_it.second);
    }

    Problem Problem::Trim(boost::posix_time::ptime begin, boost::posix_time::ptime::time_duration_type duration) const {
        const auto end = begin + duration;

        std::vector<rows::CalendarVisit> visits_to_use;
        for (const auto &visit : visits_) {
            if (begin <= visit.datetime() && visit.datetime() < end) {
                visits_to_use.push_back(visit);
            }
        }

        std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > carers_to_use;
        for (const auto &carer_diaries : carers_) {
            for (const auto &diary : carer_diaries.second) {
                if (begin.date() <= diary.date() && diary.date() <= end.date()) {
                    carers_to_use.emplace_back(carer_diaries.first, std::vector<rows::Diary>{diary});
                }
            }
        }

        return Problem(visits_to_use, carers_to_use);
    }

    void Problem::RemoveCancelled(const std::vector<rows::ScheduledVisit> &visits) {
        std::vector<rows::CalendarVisit> visits_to_use;

        std::unordered_map<rows::ServiceUser, std::vector<rows::ScheduledVisit> > cancelled_visits;
        for (const auto &scheduled_visit : visits) {
            if (scheduled_visit.type() != rows::ScheduledVisit::VisitType::CANCELLED) {
                continue;
            }

            const auto &calendar_visit = scheduled_visit.calendar_visit();
            if (!calendar_visit) {
                continue;
            }

            const auto &service_user = calendar_visit.get().service_user();
            auto bucket_pair = cancelled_visits.find(service_user);
            if (bucket_pair == std::end(cancelled_visits)) {
                cancelled_visits.insert(std::make_pair(service_user, std::vector<rows::ScheduledVisit>{}));
                bucket_pair = cancelled_visits.find(service_user);
            }
            bucket_pair->second.push_back(scheduled_visit);
        }

        for (const auto &visit : visits_) {
            const auto find_it = cancelled_visits.find(visit.service_user());
            if (find_it != std::end(cancelled_visits)) {
                const auto found_it = std::find_if(std::begin(find_it->second), std::end(find_it->second),
                                                   [&visit](const rows::ScheduledVisit &cancelled_visit) -> bool {
                                                       const auto &local_visit = cancelled_visit.calendar_visit().get();
                                                       return visit.service_user() == local_visit.service_user()
                                                              && visit.datetime() == local_visit.datetime()
                                                              && visit.address() == local_visit.address();
                                                   });

                if (found_it != std::end(find_it->second)) {
                    continue;
                }
            }

            visits_to_use.push_back(visit);
        }

        visits_ = visits_to_use;
    }

    const boost::optional<Diary> Problem::diary(const Carer &carer, boost::posix_time::ptime::date_type date) const {
        for (const auto &carer_diary_pair : carers_) {
            if (carer_diary_pair.first != carer) {
                continue;
            }

            for (const auto &diary : carer_diary_pair.second) {
                if (diary.date() == date) {
                    return boost::make_optional(diary);
                }
            }
        }

        return boost::optional<Diary>();
    }
}