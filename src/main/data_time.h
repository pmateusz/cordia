#ifndef ROWS_DATA_TIME_H
#define ROWS_DATA_TIME_H

#include <boost/date_time/posix_time/posix_time.hpp>

#include "util/json.h"

namespace rows {
    namespace DateTime {
        class JsonLoader : protected rows::JsonLoader {
        public:
            template<typename JsonType>
            boost::posix_time::ptime Load(const JsonType &document) const;
        };
    }
}

namespace rows::DateTime {

    template<typename JsonType>
    boost::posix_time::ptime JsonLoader::Load(const JsonType &document) const {
        const auto date_it = document.find("date");
        if (date_it == std::end(document)) { throw OnKeyNotFound("date"); }
        auto date = boost::gregorian::from_simple_string(date_it.value().template get<std::string>());

        const auto time_it = document.find("time");
        if (time_it == std::end(document)) { throw OnKeyNotFound("time"); }
        const auto time_of_day = boost::posix_time::duration_from_string(time_it.value().template get<std::string>());
        return {date, time_of_day};
    }
}

#endif //ROWS_DATA_TIME_H
