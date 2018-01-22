#ifndef ROWS_PROBLEM_H
#define ROWS_PROBLEM_H

#include <utility>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <string>
#include <functional>

#include <glog/logging.h>
#include <boost/optional.hpp>
#include <boost/date_time.hpp>

#include "carer.h"
#include "calendar_visit.h"
#include "scheduled_visit.h"
#include "service_user.h"
#include "diary.h"
#include "location.h"
#include "json.h"
#include "data_time.h"

namespace rows {

    class Problem {
    public:
        Problem() = default;

        Problem(std::vector<CalendarVisit> visits,
                std::vector<std::pair<Carer, std::vector<Diary> > > carers,
                std::vector<ExtendedServiceUser> service_users);

        std::pair<boost::posix_time::ptime, boost::posix_time::ptime> Timespan() const;

        Problem Trim(boost::posix_time::ptime begin, boost::posix_time::ptime::time_duration_type duration) const;

        const std::vector<CalendarVisit> &visits() const;

        const std::vector<std::pair<Carer, std::vector<Diary> > > &carers() const;

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
        };

        void RemoveCancelled(const std::vector<rows::ScheduledVisit> &visits);

    private:
        std::vector<CalendarVisit> visits_;
        std::vector<std::pair<Carer, std::vector<Diary> > > carers_;
        std::vector<ExtendedServiceUser> service_users_;
    };
}

namespace rows {

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
            auto key = key_it.value().template get<std::string>();

            const auto address_it = service_user_json.find("address");
            if (address_it == std::end(service_user_json)) { throw OnKeyNotFound("address"); }
            auto address = address_loader.Load(address_it.value());

            const auto location_it = service_user_json.find("location");
            if (location_it == std::end(service_user_json)) { throw OnKeyNotFound("location"); }
            auto location = location_loader.Load(location_it.value());

            std::unordered_map<Carer, double> carer_preference;
            const auto carer_preference_it = service_user_json.find("carer_preference");
            if (carer_preference_it == std::end(service_user_json)) { throw OnKeyNotFound("carer_preference"); }
            for (const auto &carer_preference_row : carer_preference_it.value()) {
                Carer carer{carer_preference_row.at(0).template get<std::string>()};
                const auto preference = carer_preference_row.at(1).template get<double>();
                const auto insert_pair = carer_preference.emplace(std::move(carer), preference);
                DCHECK(insert_pair.second);
            }

            service_users.emplace_back(std::move(key),
                                       std::move(address),
                                       std::move(location),
                                       std::move(carer_preference));
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
            const ServiceUser service_user{service_user_it.value().template get<std::string>()};

            const auto service_user_index_it = service_user_index.find(service_user);
            DCHECK(service_user_index_it != std::cend(service_user_index));

            const Address address{service_user_index_it->second.first};
            const Location location{service_user_index_it->second.second};

            const auto visits_it = group_visits_json.find("visits");
            if (visits_it == std::end(group_visits_json)) { throw OnKeyNotFound("visits"); }
            for (const auto &visit_json : visits_it.value()) {
                auto date_time = datetime_loader.Load(visit_json);
                const auto duration_it = visit_json.find("duration");
                if (duration_it == std::end(visit_json)) { throw OnKeyNotFound("duration"); }
                auto duration = boost::posix_time::seconds(std::stol(duration_it.value().template get<std::string>()));

                result.emplace_back(service_user, address, boost::make_optional(location), date_time, duration);
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

            rows::Carer carer(sap_number_it.value().template get<std::string>());

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

                diaries.emplace_back(date, events);
            }

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

        return Problem(std::move(visits), std::move(carers), std::move(service_users));
    }
}
#endif //ROWS_PROBLEM_H
