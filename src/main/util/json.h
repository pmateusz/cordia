#ifndef ROWS_JSON_H
#define ROWS_JSON_H


#include <nlohmann/json.hpp>
#include <boost/date_time.hpp>

namespace boost {

    namespace posix_time {

        void to_json(nlohmann::json &json, const ptime &value);

        void from_json(const nlohmann::json &json, ptime &value);

        void to_json(nlohmann::json &json, const time_duration &value);

        void from_json(const nlohmann::json &json, time_duration &value);

        void to_json(nlohmann::json &json, const time_period &value);

        void from_json(const nlohmann::json &json, time_period &value);
    }
}


#endif //ROWS_JSON_H
