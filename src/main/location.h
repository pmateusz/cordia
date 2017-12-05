#ifndef ROWS_LOCATION_H
#define ROWS_LOCATION_H

#include <cstddef>
#include <string>
#include <ostream>

#include <boost/functional/hash.hpp>

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

        const std::string &latitude() const;

        const std::string &longitude() const;

        friend struct std::hash<rows::Location>;

        friend std::ostream &operator<<(std::ostream &out, const Location &object);

    private:
        std::string longitude_;
        std::string latitude_;
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
            boost::hash_combine(seed, object.latitude_);
            boost::hash_combine(seed, object.longitude_);
            return seed;
        }
    };

}

#endif //ROWS_LOCATION_H
