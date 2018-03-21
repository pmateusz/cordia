#include <boost/format.hpp>

#include "location.h"

namespace rows {

    Location::Location()
            : longitude_(osrm::util::FixedLongitude()),
              latitude_(osrm::util::FixedLatitude()) {}

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
        static const auto COORDINATE_PRECISION = static_cast<int32_t>(osrm::COORDINATE_PRECISION);
        static const auto DECIMAL_PLACES = static_cast<int32_t>(std::log10(osrm::COORDINATE_PRECISION));

        if (text.empty()) {
            return 0;
        }
        const auto dot_position = text.find('.');
        if (dot_position == std::string::npos) {
            return std::stoi(text) * COORDINATE_PRECISION;
        }

        const std::string raw_value_part{text.substr(0, dot_position)};
        const std::string raw_decimal_part{text.substr(dot_position + 1, static_cast<unsigned long>(DECIMAL_PLACES))};

        const auto value_part = std::stol(raw_value_part) * COORDINATE_PRECISION;
        const auto decimal_factor = static_cast<long>(std::pow(10.0, DECIMAL_PLACES - raw_decimal_part.size()));
        const auto decimal_part = std::stol(raw_decimal_part) * decimal_factor;

        auto decimal_coefficient = 1l;
        if (value_part < 0) {
            decimal_coefficient = -1l;
        }

        return static_cast<std::int32_t>(value_part + decimal_coefficient * decimal_part);
    }


    std::ostream &operator<<(std::ostream &out, const Location &object) {
        out << boost::format("(%1%, %2%)") % object.latitude_ % object.longitude_;
        return out;
    }

    Location::CartesianCoordinates Location::ToCartesianCoordinates(const osrm::FixedLatitude &latitude,
                                                                    const osrm::FixedLongitude &longitude) {
        const auto latitude_to_use = (static_cast<double>(osrm::toFloating(latitude)) * M_PI) / 180.0;
        const auto longitude_to_use = (static_cast<double>(osrm::toFloating(longitude)) * M_PI) / 180.0;

        double x = cos(latitude_to_use) * cos(longitude_to_use);
        double y = cos(latitude_to_use) * sin(longitude_to_use);
        double z = sin(latitude_to_use);
        return {x, y, z};
    }

    std::pair<osrm::FixedLatitude, osrm::FixedLongitude> Location::ToGeographicCoordinates(
            const Location::CartesianCoordinates &coordinates) {
        const auto longitude = (atan2(std::get<1>(coordinates), std::get<0>(coordinates)) * 180.0) / M_PI;
        const auto hyperplane = sqrt(pow(std::get<0>(coordinates), 2.0) + pow(std::get<1>(coordinates), 2.0));
        const auto latitude = (atan2(std::get<2>(coordinates), hyperplane) * 180.0) / M_PI;

        return std::make_pair(osrm::toFixed(osrm::util::FloatLatitude{latitude}),
                              osrm::toFixed(osrm::util::FloatLongitude{longitude}));
    }

    Location::CartesianCoordinates Location::GetCentralPoint(const std::vector<CartesianCoordinates> &points) {
        double x = 0.0, y = 0.0, z = 0.0;
        for (const auto &point : points) {
            x += std::get<0>(point);
            y += std::get<1>(point);
            z += std::get<2>(point);
        }
        const double points_count = std::max(static_cast<double>(points.size()), 1.0);
        return {x / points_count, y / points_count, z / points_count};
    }
}
