#ifndef ROWS_SOLUTION_H
#define ROWS_SOLUTION_H

#include <string>
#include <exception>
#include <stdexcept>

#include <glog/logging.h>

#include <boost/optional.hpp>
#include <boost/date_time.hpp>

#include "carer.h"
#include "calendar_visit.h"
#include "address.h"
#include "service_user.h"
#include "data_time.h"
#include "scheduled_visit.h"

namespace rows {

    class Solution {
    public:

        class JsonLoader : protected rows::JsonLoader {
        public:
            /*!
             * @throws std::domain_error
             */
            template<typename JsonType>
            Solution Load(const JsonType &document);

        private:
            std::domain_error OnKeyNotFound(std::string key);
        };
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

        for (const auto &actual_visit : visits_it.value()) {
            LOG(INFO) << visit_loader.Load(actual_visit);
        }

        return Solution();
    }
}

#endif //ROWS_SOLUTION_H
