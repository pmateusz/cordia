#include "json.h"

#include <boost/format.hpp>
#include <glog/logging.h>

std::domain_error rows::JsonLoader::OnKeyNotFound(std::string key) const {
    return std::domain_error((boost::format("Key '%1%' not found") % key).str());
}


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
    const auto raw_duration = json.get<std::string>();
    if (raw_duration.find(':') == std::string::npos) {
        value = boost::posix_time::seconds(std::stol(raw_duration));
    } else {
        value = boost::posix_time::duration_from_string(raw_duration);
    }
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

void boost::gregorian::from_json(const nlohmann::json &json, boost::gregorian::date &value) {
    const auto raw_date = json.get<std::string>();
    value = boost::gregorian::from_string(raw_date);
}
