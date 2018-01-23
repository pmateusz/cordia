#include "location.h"

#include <boost/format.hpp>

namespace rows {

    Location::Location(std::string latitude, std::string longitude)
            : latitude_{ToFixedValue(latitude)},
              longitude_{ToFixedValue(longitude)} {}

    rows::Location::Location(osrm::util::FixedLatitude latitude, osrm::util::FixedLongitude longitude)
            : latitude_(latitude),
              longitude_(longitude) {}

    Location::Location(const Location &other)
            : latitude_(other.latitude_),
              longitude_(other.longitude_) {}

    Location::Location(Location &&other) noexcept
            : latitude_(other.latitude_),
              longitude_(other.longitude_) {}

    Location &Location::operator=(const Location &other) {
        latitude_ = other.latitude_;
        longitude_ = other.longitude_;
        return *this;
    }

    Location &Location::operator=(Location &&other) noexcept {
        latitude_ = other.latitude_;
        longitude_ = other.longitude_;
        return *this;
    }

    bool Location::operator==(const Location &other) const {
        return latitude_ == other.latitude_
               && longitude_ == other.longitude_;
    }

    bool Location::operator!=(const Location &other) const {
        return !operator==(other);
    }

    const osrm::util::FixedLatitude &Location::latitude() const {
        return latitude_;
    }

    const osrm::util::FixedLongitude &Location::longitude() const {
        return longitude_;
    }

    std::int32_t Location::ToFixedValue(const std::string &text) {
        if (text.empty()) {
            return 0;
        }

        static const auto COORDINATE_PRECISION = static_cast<int32_t>(osrm::COORDINATE_PRECISION);
        static const auto DECIMAL_PLACES = static_cast<int32_t>(std::log10(osrm::COORDINATE_PRECISION));

        const auto dot_position = text.find('.');
        if (dot_position == std::string::npos) {
            return std::stoi(text) * COORDINATE_PRECISION;
        }

        const auto value_part = text.substr(0, dot_position);
        const auto decimal_part = text.substr(dot_position + 1, static_cast<unsigned long>(DECIMAL_PLACES));
        const auto decimal_factor = static_cast<long>(std::pow(10.0, DECIMAL_PLACES - decimal_part.size()));
        return static_cast<int32_t>(std::stol(value_part) * COORDINATE_PRECISION
                                    + std::stol(decimal_part) * decimal_factor);
    }

    std::ostream &operator<<(std::ostream &out, const Location &object) {
        out << boost::format("(%1%, %2%)") % object.latitude_ % object.longitude_;
        return out;
    }
}
