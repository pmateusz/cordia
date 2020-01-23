#ifndef ROWS_PROBLEM_H
#define ROWS_PROBLEM_H

#include <algorithm>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <stdexcept>
#include <string>
#include <functional>

#include <glog/logging.h>
#include <boost/optional.hpp>
#include <boost/date_time.hpp>
#include <nlohmann/json.hpp>

#include "carer.h"
#include "calendar_visit.h"
#include "scheduled_visit.h"
#include "service_user.h"
#include "diary.h"
#include "location.h"
#include "json.h"
#include "data_time.h"
#include "util/aplication_error.h"

namespace rows {

    class Problem {
    public:
        struct PartialVisitOperations {
            std::size_t operator()(const rows::CalendarVisit &object) const noexcept;

            bool operator()(const rows::CalendarVisit &left, const rows::CalendarVisit &right) const noexcept;
        };

        Problem() = default;

        Problem(std::vector<CalendarVisit> visits,
                std::vector<std::pair<Carer, std::vector<Diary> > > carers,
                std::vector<ExtendedServiceUser> service_users);

        std::pair<boost::posix_time::ptime, boost::posix_time::ptime> Timespan() const;

        Problem Trim(boost::posix_time::ptime begin, boost::posix_time::ptime::time_duration_type duration) const;

        const std::vector<CalendarVisit> &visits() const;

        template<typename PredicateType>
        std::vector<rows::CalendarVisit> Visits(const PredicateType &predicate) const;

        const std::vector<std::pair<Carer, std::vector<Diary> > > &carers() const;

        const std::vector<ExtendedServiceUser> &service_users() const;

        const boost::optional<Diary> diary(const Carer &carer, boost::posix_time::ptime::date_type date) const;

        /*!
         * Runs fast checks to test if the problem can be solved
         * @return
         */
        bool IsAdmissible() const;

        class JsonLoader : protected rows::JsonLoader {
        public:
            /*!
             * @throws std::domain_error
             */
            template<typename JsonType>
            Problem Load(const JsonType &document);

        private:
            template<typename JsonType>
            std::vector<ExtendedServiceUser> LoadServiceUsers(const JsonType &document) const;

            template<typename JsonType>
            std::vector<CalendarVisit> LoadVisits(const JsonType &document,
                                                  const std::unordered_map<ServiceUser,
                                                          std::pair<rows::Address, rows::Location> > &service_user_index) const;

            template<typename JsonType>
            std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > LoadCarers(const JsonType &document);

            std::domain_error OnUserPropertyNotSet(std::string item_key, long user_key) const;
        };

        void RemoveCancelled(const std::vector<rows::ScheduledVisit> &visits);

    private:
        std::vector<CalendarVisit> visits_;
        std::vector<std::pair<Carer, std::vector<Diary> > > carers_;
        std::vector<ExtendedServiceUser> service_users_;
    };
}

namespace rows {

    template<typename PredicateType>
    std::vector<rows::CalendarVisit> Problem::Visits(const PredicateType &predicate) const {
        std::vector<rows::CalendarVisit> result;
        for (const auto &visit: visits_) {
            if (predicate(visit)) {
                result.push_back(visit);
            }
        }
        return result;
    }

    template<typename JsonType>
    std::vector<ExtendedServiceUser> Problem::JsonLoader::LoadServiceUsers(const JsonType &json) const {
        static const rows::Location::JsonLoader location_loader{};
        static const rows::Address::JsonLoader address_loader{};

        std::vector<ExtendedServiceUser> service_users;

        const auto service_users_it = json.find("service_users");
        if (service_users_it == std::end(json)) { throw OnKeyNotFound("service_users"); }

        for (const auto &service_user_json : service_users_it.value()) {
            const auto key_it = service_user_json.find("key");
            if (key_it == std::end(service_user_json)) { throw OnKeyNotFound("key"); }
            auto key = std::stol(key_it.value().template get<std::string>());

            const auto address_it = service_user_json.find("address");
            if (address_it == std::end(service_user_json)) { throw OnUserPropertyNotSet("address", key); }
            auto address = address_loader.Load(address_it.value());

            const auto location_it = service_user_json.find("location");
            if (location_it == std::end(service_user_json)) { throw OnUserPropertyNotSet("location", key); }
            rows::Location location;
            try {
                location = location_loader.Load(location_it.value());
            } catch (const std::domain_error &ex) {
                throw std::domain_error(
                        (boost::format("Failed to load property location of the user '%1%' due to error: %2%")
                         % key
                         % ex.what()).str());
            }

            std::unordered_map<Carer, double> carer_preference;
            const auto carer_preference_it = service_user_json.find("carer_preference");
            if (carer_preference_it == std::end(service_user_json)) {
                throw OnUserPropertyNotSet("carer_preference", key);
            }

            for (const auto &carer_preference_row : carer_preference_it.value()) {
                Carer carer{carer_preference_row.at(0).template get<std::string>()};
                const auto preference = carer_preference_row.at(1).template get<double>();
                const auto insert_pair = carer_preference.emplace(std::move(carer), preference);
                DCHECK(insert_pair.second);
            }

            service_users.emplace_back(std::move(key),
                                       std::move(address),
                                       std::move(location));
        }

        return service_users;
    }

    template<typename JsonType>
    std::vector<rows::CalendarVisit> Problem::JsonLoader::LoadVisits(const JsonType &json,
                                                                     const std::unordered_map<ServiceUser,
                                                                             std::pair<rows::Address, rows::Location> > &service_user_index) const {
        static const DateTime::JsonLoader datetime_loader{};

        std::vector<rows::CalendarVisit> result;

        const auto group_visits_it = json.find("visits");
        if (group_visits_it == std::end(json)) { throw OnKeyNotFound("visits"); }

        for (const auto &group_visits_json : group_visits_it.value()) {
            const auto service_user_it = group_visits_json.find("service_user");
            if (service_user_it == std::end(group_visits_json)) { throw OnKeyNotFound("service_user"); }
            const ServiceUser service_user{std::stol(service_user_it.value().template get<std::string>())};

            const auto service_user_index_it = service_user_index.find(service_user);
            DCHECK(service_user_index_it != std::end(service_user_index));

            const Address address{service_user_index_it->second.first};
            const Location location{service_user_index_it->second.second};

            const auto visits_it = group_visits_json.find("visits");
            if (visits_it == std::end(group_visits_json)) { throw OnKeyNotFound("visits"); }
            for (const auto &visit_json : visits_it.value()) {
                std::size_t key;
                const auto key_it = visit_json.find("key");
                if (key_it == std::end(visit_json)) { throw OnKeyNotFound("key"); }
                key = key_it.value().template get<std::size_t>();

                auto date_time = datetime_loader.Load(visit_json);
                const auto duration_it = visit_json.find("duration");
                if (duration_it == std::end(visit_json)) { throw OnKeyNotFound("duration"); }

                boost::posix_time::seconds duration{0};
                if (duration_it.value().is_string()) {
                    duration = boost::posix_time::seconds(std::stol(duration_it.value().template get<std::string>()));
                } else if (duration_it.value().is_number()) {
                    duration = boost::posix_time::seconds(duration_it.value().template get<int>());
                } else {
                    throw std::domain_error(
                            (boost::format("Unknown format of duration %s") % duration_it.value()).str());
                }

                std::vector<int> tasks;
                const auto tasks_it = visit_json.find("tasks");
                if (tasks_it != std::end(visit_json)) {
                    tasks = tasks_it.value().template get<std::vector<int>>();
                }

                const auto carer_count_it = visit_json.find("carer_count");
                if (duration_it == std::end(visit_json)) { throw OnKeyNotFound("carer_count"); }
                auto carer_count = carer_count_it.value().template get<int>();
                result.emplace_back(key,
                                    service_user,
                                    address,
                                    boost::make_optional(location),
                                    date_time,
                                    duration,
                                    carer_count,
                                    std::move(tasks));
            }
        }

        return result;
    }

    template<typename JsonType>
    std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > >
    Problem::JsonLoader::LoadCarers(const JsonType &json) {
        std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > result;

        const auto carers_it = json.find("carers");
        if (carers_it == std::end(json)) { throw OnKeyNotFound("carers"); }

        for (const auto &carer_json_group : carers_it.value()) {
            const auto carer_json_it = carer_json_group.find("carer");
            if (carer_json_it == std::end(carer_json_group)) { throw OnKeyNotFound("carer"); }

            const auto &carer_json = carer_json_it.value();
            const auto sap_number_it = carer_json.find("sap_number");
            if (sap_number_it == std::end(carer_json)) { throw OnKeyNotFound("sap_number"); }
            const auto sap_number = sap_number_it.value().template get<std::string>();

            Transport transport = Transport::Foot;
            const auto transport_it = carer_json.find("mobility");
            if (transport_it != std::end(carer_json)) {
                transport = ParseTransport(transport_it.value().template get<std::string>());
            }

            std::vector<int> skills;
            const auto skills_it = carer_json.find("skills");
            if (skills_it != std::end(carer_json)) {
                skills = skills_it.value().template get<std::vector<int>>();
            }

            rows::Carer carer(sap_number, transport, skills);

            const auto diaries_it = carer_json_group.find("diaries");
            if (diaries_it == std::end(carer_json_group)) { throw OnKeyNotFound("diaries"); }

            std::vector<rows::Diary> diaries;
            for (const auto &diary : diaries_it.value()) {
                const auto date_it = diary.find("date");
                if (date_it == std::end(diary)) { throw OnKeyNotFound("date"); }
                auto date = boost::gregorian::from_simple_string(date_it.value().template get<std::string>());

                std::vector<rows::Event> events;
                const auto events_it = diary.find("events");
                if (events_it == std::end(diary)) { throw OnKeyNotFound("events"); }

                for (const auto &event : events_it.value()) {
                    const auto begin_it = event.find("begin");
                    if (begin_it == std::end(event)) { throw OnKeyNotFound("begin"); }

                    boost::posix_time::ptime begin = boost::date_time::parse_delimited_time<boost::posix_time::ptime>(
                            begin_it.value().template get<std::string>(), 'T');

                    const auto end_it = event.find("end");
                    if (end_it == std::end(event)) { throw OnKeyNotFound("end"); }

                    boost::posix_time::ptime end = boost::date_time::parse_delimited_time<boost::posix_time::ptime>(
                            end_it.value().template get<std::string>(), 'T');

                    events.emplace_back(boost::posix_time::time_period(begin, end));
                }
                std::sort(std::begin(events), std::end(events),
                          [](const rows::Event &left, const rows::Event &right) -> bool {
                              return left.begin() <= right.begin();
                          });

                diaries.emplace_back(date, events);
            }
            std::sort(std::begin(diaries), std::end(diaries),
                      [](const rows::Diary &left, const rows::Diary &right) -> bool {
                          return left.date() <= right.date();
                      });

            result.emplace_back(carer, diaries);
        }

        return result;
    }

    template<typename JsonType>
    Problem Problem::JsonLoader::Load(const JsonType &document) {
        const auto service_users = LoadServiceUsers(document);

        std::unordered_map<ServiceUser, std::pair<Address, Location> > service_user_index;
        for (const auto &service_user : service_users) {
            const auto inserted = service_user_index.insert(
                    std::make_pair(service_user, std::make_pair(service_user.address(), service_user.location())));
            CHECK(inserted.second);
        }

        const auto visits = LoadVisits(document, service_user_index);
        const auto carers = LoadCarers(document);

        std::unordered_set<rows::CalendarVisit,
                Problem::PartialVisitOperations,
                Problem::PartialVisitOperations> visit_index;
        for (const auto &visit : visits) {
            if (!visit_index.insert(visit).second) {
                throw util::ApplicationError(
                        (boost::format("Problem definition contains duplicate visit %1% at service user %2%")
                         % visit.datetime()
                         % visit.service_user()).str(), util::ErrorCode::ERROR);
            }
        }

        return Problem(std::move(visits), std::move(carers), std::move(service_users));
    }
}
#endif //ROWS_PROBLEM_H
