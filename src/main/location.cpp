#include "location.h"

#include <boost/format.hpp>

namespace rows {

    Location::Location(std::string latitude, std::string longitude)
            : latitude_{std::stod(latitude)},
              longitude_{std::stod(longitude)} {}

    rows::Location::Location(osrm::util::FloatLatitude latitude, osrm::util::FloatLongitude longitude)
            : latitude_(latitude),
              longitude_(longitude) {}

    Location::Location(const Location &other)
            : latitude_(other.latitude_),
              longitude_(other.longitude_) {}

    Location::Location(Location &&other) noexcept
            : latitude_(other.latitude_),
              longitude_(other.longitude_) {}

    Location &Location::operator=(const Location &other) {
        latitude_ == other.latitude_;
        longitude_ == other.longitude_;
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

    const osrm::util::FloatLatitude &Location::latitude() const {
        return latitude_;
    }

    const osrm::util::FloatLongitude &Location::longitude() const {
        return longitude_;
    }

    std::ostream &operator<<(std::ostream &out, const Location &object) {
        out << boost::format("(%1%, %2%)") % object.latitude_ % object.longitude_;
        return out;
    }
}
