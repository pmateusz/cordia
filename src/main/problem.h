#ifndef ROWS_PROBLEM_H
#define ROWS_PROBLEM_H

#include <utility>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <string>

#include "carer.h"
#include "visit.h"
#include "diary.h"
#include "location.h"

namespace rows {

    class Problem {
    public:
        Problem() = default;

        Problem(std::vector<Visit> visits, std::vector<std::pair<Carer, std::vector<Diary> > > carers);

        const std::vector<Visit> &visits() const;

        const std::vector<std::pair<Carer, std::vector<Diary> > > &carers() const;

        /*!
         * Runs fast checks to test if the problem can be solved
         * @return
         */
        bool IsAdmissible() const;

        class JsonLoader {
        public:
            /*!
             * @throws std::domain_error
             */
            template<typename JsonType>
            Problem Load(const JsonType &document);

        private:
            std::domain_error OnKeyNotFound(std::string key);

            template<typename JsonType>
            std::vector<Visit> LoadVisits(const JsonType &document);

            template<typename JsonType>
            std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > LoadCarers(const JsonType &document);
        };

    private:
        std::vector<Visit> visits_;
        std::vector<std::pair<Carer, std::vector<Diary> > > carers_;
    };
}

namespace rows {

    template<typename JsonType>
    std::vector<rows::Visit> Problem::JsonLoader::LoadVisits(const JsonType &json) {
        std::vector<rows::Visit> result;

        const auto place_visits_it = json.find("visits");
        if (place_visits_it == std::end(json)) { throw OnKeyNotFound("visits"); }

        const auto carers_it = json.find("carers");
        if (carers_it == std::end(json)) { throw OnKeyNotFound("carers"); }

        for (const auto &place_visits : place_visits_it.value()) {
            const auto location_json_it = place_visits.find("location");
            if (location_json_it == std::end(place_visits)) { throw OnKeyNotFound("location"); }

            const auto &location_json = location_json_it.value();
            const auto latitude_it = location_json.find("latitude");
            if (latitude_it == std::end(location_json)) { throw OnKeyNotFound("latitude"); }
            const auto latitude = latitude_it.value().template get<std::string>();

            const auto longitude_it = location_json.find("longitude");
            if (longitude_it == std::end(location_json)) { throw OnKeyNotFound("longitude"); }
            const auto longitude = longitude_it.value().template get<std::string>();

            rows::Location location(latitude, longitude);

            const auto visits_json_it = place_visits.find("visits");
            if (visits_json_it == std::end(place_visits)) { throw OnKeyNotFound("visits"); }
            for (const auto &visit_json : visits_json_it.value()) {
                const auto date_it = visit_json.find("date");
                if (date_it == std::end(visit_json)) { throw OnKeyNotFound("date"); }
                auto date = boost::gregorian::from_simple_string(date_it.value().template get<std::string>());

                const auto time_it = visit_json.find("time");
                if (time_it == std::end(visit_json)) { throw OnKeyNotFound("time"); }
                const auto time = boost::posix_time::duration_from_string(time_it.value().template get<std::string>());

                const auto duration_it = visit_json.find("duration");
                if (duration_it == std::end(visit_json)) { throw OnKeyNotFound("duration"); }
                boost::posix_time::time_duration duration = boost::posix_time::seconds(
                        std::stol(duration_it.value().template get<std::string>()));

                result.emplace_back(location, date, time, duration);
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

                    const auto raw_begin = begin_it.value().template get<std::string>();
                    boost::posix_time::ptime begin = boost::posix_time::from_iso_extended_string(raw_begin);

                    const auto end_it = event.find("end");
                    if (end_it == std::end(event)) { throw OnKeyNotFound("end"); }

                    boost::posix_time::ptime end = boost::posix_time::from_iso_extended_string(
                            end_it.value().template get<std::string>());

                    events.emplace_back(begin, end);
                }

                diaries.emplace_back(date, events);
            }

            result.emplace_back(carer, diaries);
        }

        return result;
    }

    template<typename JsonType>
    Problem Problem::JsonLoader::Load(const JsonType &document) {
        const auto visits = LoadVisits(document);
        const auto carers = LoadCarers(document);

        return Problem(std::move(visits), std::move(carers));
    }
}
#endif //ROWS_PROBLEM_H
