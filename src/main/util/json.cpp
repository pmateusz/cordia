#include "json.h"

void boost::posix_time::to_json(nlohmann::json &json, const boost::posix_time::ptime &value) {
    json = boost::posix_time::to_simple_string(value);
}

void boost::posix_time::from_json(const nlohmann::json &json, boost::posix_time::ptime &value) {
    const auto string_value = json.get<std::string>();
    if (string_value.find('T') != std::string::npos) {
        value = boost::posix_time::from_iso_extended_string(string_value);
    } else {
        value = boost::posix_time::time_from_string(string_value);
    }
}

void boost::posix_time::to_json(nlohmann::json &json, const boost::posix_time::time_duration &value) {
    json = boost::posix_time::to_simple_string(value);
}

void boost::posix_time::from_json(const nlohmann::json &json, boost::posix_time::time_duration &value) {
    value = boost::posix_time::duration_from_string(json.get<std::string>());
}

void boost::posix_time::to_json(nlohmann::json &json, const boost::posix_time::time_period &value) {
    nlohmann::json object;
    object["begin"] = value.begin();
    object["end"] = value.end();
    json = object;
}

void boost::posix_time::from_json(const nlohmann::json &json, boost::posix_time::time_period &value) {
    value = {json.at("begin").get<boost::posix_time::ptime>(), json.at("end").get<boost::posix_time::ptime>()};
}