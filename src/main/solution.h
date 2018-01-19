#ifndef ROWS_SOLUTION_H
#define ROWS_SOLUTION_H

#include <string>
#include <memory>
#include <vector>
#include <exception>
#include <stdexcept>

#include <glog/logging.h>

#include <boost/optional.hpp>
#include <boost/date_time.hpp>

#include "scheduled_visit.h"

namespace rows {

    class Carer;

    class Route;

    class Solution {
    public:
        explicit Solution(std::vector<ScheduledVisit> visits);

        Solution Trim(boost::posix_time::ptime begin, boost::posix_time::ptime::time_duration_type duration) const;

        class JsonLoader : protected rows::JsonLoader {
        public:
            /*!
             * @throws std::domain_error
             */
            template<typename JsonType>
            Solution Load(const JsonType &document);
        };

        const std::vector<Carer> Carers() const;

        Route GetRoute(const Carer &carer) const;

        void UpdateVisitLocations(const std::vector<CalendarVisit> &visits);

        const std::vector<ScheduledVisit> &visits() const;

    private:
        std::vector<ScheduledVisit> visits_;
    };
}

namespace rows {

    template<typename JsonType>
    Solution Solution::JsonLoader::Load(const JsonType &document) {
        static const ScheduledVisit::JsonLoader visit_loader{};

        auto visits_it = document.find("visits");
        if (visits_it == std::end(document)) {
            throw OnKeyNotFound("visits");
        }

        std::vector<ScheduledVisit> visits;
        for (const auto &actual_visit : visits_it.value()) {
            visits.emplace_back(visit_loader.Load(actual_visit));
        }
        return Solution(std::move(visits));
    }
}

#endif //ROWS_SOLUTION_H
