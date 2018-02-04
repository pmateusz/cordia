#ifndef ROWS_LOCATION_H
#define ROWS_LOCATION_H

#include <cstddef>
#include <cstdint>
#include <utility>
#include <string>
#include <ostream>

#include <boost/functional/hash.hpp>
#include <osrm/util/alias.hpp>
#include <osrm/coordinate.hpp>

#include "json.h"


namespace rows {

    class Location {
    public:
        Location(std::string latitude, std::string longitude);

        Location(osrm::util::FixedLatitude latitude, osrm::util::FixedLongitude longitude);

        Location(const Location &other);

        Location(Location &&other) noexcept;

        Location &operator=(const Location &other);

        Location &operator=(Location &&other) noexcept;

        bool operator==(const Location &other) const;

        bool operator!=(const Location &other) const;

        class JsonLoader : protected rows::JsonLoader {
        public:
            template<typename JsonType>
            Location Load(JsonType document) const;
        };

        const osrm::util::FixedLatitude &latitude() const;

        const osrm::util::FixedLongitude &longitude() const;

        friend struct std::hash<rows::Location>;

        friend std::ostream &operator<<(std::ostream &out, const Location &object);

        template<typename IteratorType>
        static Location GetCentralLocation(IteratorType begin_it, IteratorType end_it);

    private:
        typedef std::tuple<double, double, double> CartesianCoordinates;

        static CartesianCoordinates ToCartesianCoordinates(const osrm::FixedLatitude &latitude,
                                                           const osrm::FixedLongitude &longitude);

        static std::pair<osrm::FixedLatitude, osrm::FixedLongitude> ToGeographicCoordinates(
                const Location::CartesianCoordinates &coordinates);

        static CartesianCoordinates GetCentralPoint(const std::vector<CartesianCoordinates> &points);

        osrm::util::FixedLatitude latitude_;
        osrm::util::FixedLongitude longitude_;

        static int32_t ToFixedValue(const std::string &text);
    };
}

namespace rows {

    template<typename JsonType>
    Location Location::JsonLoader::Load(JsonType document) const {
        const auto latitude_it = document.find("latitude");
        if (latitude_it == std::end(document)) { throw OnKeyNotFound("latitude"); }
        const auto latitude = latitude_it.value().template get<std::string>();

        const auto longitude_it = document.find("longitude");
        if (longitude_it == std::end(document)) { throw OnKeyNotFound("longitude"); }
        const auto longitude = longitude_it.value().template get<std::string>();
        return {latitude, longitude};
    }

    template<typename IteratorType>
    Location Location::GetCentralLocation(IteratorType begin_it, IteratorType end_it) {
        std::vector<CartesianCoordinates> points;

        for (auto it = begin_it; it != end_it; ++it) {
            points.push_back(ToCartesianCoordinates(it->latitude(), it->longitude()));
        }

        const auto average_point = GetCentralPoint(points);
        std::pair<osrm::FixedLatitude, osrm::FixedLongitude> average_geo_point = ToGeographicCoordinates(
                average_point);
        return {average_geo_point.first, average_geo_point.second};
    }
}

namespace std {

    template<>
    struct hash<rows::Location> {
        typedef rows::Location argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<osrm::FixedLatitude> hash_latitude{};
            static const std::hash<osrm::FixedLongitude> hash_longitude{};

            std::size_t seed = 0;
            boost::hash_combine(seed, hash_latitude(object.latitude_));
            boost::hash_combine(seed, hash_longitude(object.longitude_));
            return seed;
        }
    };
}

#endif //ROWS_LOCATION_H
