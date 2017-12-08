#ifndef ROWS_LOCATION_H
#define ROWS_LOCATION_H

#include <cstddef>
#include <utility>
#include <string>
#include <ostream>

#include <osrm/util/alias.hpp>
#include <boost/functional/hash.hpp>
#include <osrm/coordinate.hpp>


namespace rows {

    class Location {
    public:
        Location(std::string longitude, std::string latitude);

        Location(const Location &other);

        Location(Location &&other) noexcept;

        Location &operator=(const Location &other);

        Location &operator=(Location &&other) noexcept;

        bool operator==(const Location &other) const;

        bool operator!=(const Location &other) const;

        template<typename JsonType>
        static Location from_json(const JsonType &json);

        const osrm::util::FloatLatitude &latitude() const;

        const osrm::util::FloatLongitude &longitude() const;

        friend struct std::hash<rows::Location>;

        friend std::ostream &operator<<(std::ostream &out, const Location &object);

    private:
        osrm::util::FloatLatitude latitude_;
        osrm::util::FloatLongitude longitude_;
    };
}

namespace rows {

    template<typename JsonType>
    Location Location::from_json(const JsonType &json) {
        return Location(json["latitude"], json["longitude"]);
    }
}

namespace std {

    template<>
    struct hash<rows::Location> {
        typedef rows::Location argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            std::size_t seed = 0;
            boost::hash_combine(seed, static_cast<osrm::util::FloatLatitude::value_type>(object.latitude_));
            boost::hash_combine(seed, static_cast<osrm::util::FloatLongitude::value_type>(object.longitude_));
            return seed;
        }
    };

}

#endif //ROWS_LOCATION_H
