#ifndef ROWS_PROBLEM_H
#define ROWS_PROBLEM_H

#include <utility>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <string>

#include <glog/logging.h>

#include "carer.h"
#include "calendar_visit.h"
#include "diary.h"
#include "location.h"
#include "json.h"
#include "data_time.h"

namespace rows {

    class Problem {
    public:
        Problem() = default;

        Problem(std::vector<CalendarVisit> visits, std::vector<std::pair<Carer, std::vector<Diary> > > carers);

        const std::vector<CalendarVisit> &visits() const;

        const std::vector<std::pair<Carer, std::vector<Diary> > > &carers() const;

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
            std::vector<CalendarVisit> LoadVisits(const JsonType &document);

            template<typename JsonType>
            std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > LoadCarers(const JsonType &document);
        };

    private:
        std::vector<CalendarVisit> visits_;
        std::vector<std::pair<Carer, std::vector<Diary> > > carers_;
    };
}

namespace rows {

    template<typename JsonType>
    std::vector<rows::CalendarVisit> Problem::JsonLoader::LoadVisits(const JsonType &json) {
        std::vector<rows::CalendarVisit> result;

        const auto place_visits_it = json.find("visits");
        if (place_visits_it == std::end(json)) { throw OnKeyNotFound("visits"); }

        const auto carers_it = json.find("carers");
        if (carers_it == std::end(json)) { throw OnKeyNotFound("carers"); }

        Location::JsonLoader location_loader;
        CalendarVisit::JsonLoader visit_loader;
        for (const auto &place_visits : place_visits_it.value()) {
            const auto location_json_it = place_visits.find("location");
            if (location_json_it == std::end(place_visits)) { throw OnKeyNotFound("location"); }
            rows::Location location = location_loader.Load(location_json_it.value());

            const auto visits_json_it = place_visits.find("visits");
            if (visits_json_it == std::end(place_visits)) { throw OnKeyNotFound("visits"); }
            for (const auto &visit_json : visits_json_it.value()) {
                auto visit = visit_loader.Load(visit_json);
                visit.location(location);
                result.emplace_back(visit);
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
        const auto visits = LoadVisits(document);
        const auto carers = LoadCarers(document);

        return Problem(std::move(visits), std::move(carers));
    }
}
#endif //ROWS_PROBLEM_H
