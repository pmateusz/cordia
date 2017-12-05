#ifndef ROWS_VISIT_H
#define ROWS_VISIT_H

#include <cstddef>
#include <ostream>
#include <functional>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "location.h"

namespace rows {

    class Visit {
    public:
        Visit(Location location,
              boost::gregorian::date date,
              boost::posix_time::time_duration time,
              boost::posix_time::time_duration duration);

        Visit(const Visit &other);

        Visit &operator=(const Visit &other);

        friend struct std::hash<rows::Visit>;

        friend std::ostream &operator<<(std::ostream &out, const Visit &object);

        bool operator==(const Visit &other) const;

        bool operator!=(const Visit &other) const;

        const Location &location() const;

        const boost::gregorian::date date() const;

        const boost::posix_time::time_duration time() const;

        const boost::posix_time::time_duration duration() const;

    private:
        Location location_;
        boost::gregorian::date date_;
        boost::posix_time::time_duration time_;
        boost::posix_time::time_duration duration_;
    };
}

namespace std {

    template<>
    struct hash<boost::gregorian::date> {
        typedef boost::gregorian::date argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<boost::gregorian::date::date_rep_type::int_type> hash{};
            return hash(object.day_count().as_number());
        }
    };

    template<>
    struct hash<boost::posix_time::time_duration> {
        typedef boost::posix_time::time_duration argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<boost::posix_time::time_duration::tick_type> hash{};
            return hash(object.get_rep().as_number());
        }
    };

    template<>
    struct hash<rows::Visit> {
        typedef rows::Visit argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<rows::Location> hash_location{};
            static const std::hash<boost::gregorian::date> hash_date{};
            static const std::hash<boost::posix_time::time_duration> hash_posix_duration{};

            std::size_t seed = 0;
            boost::hash_combine(seed, hash_location(object.location_));
            boost::hash_combine(seed, hash_date(object.date_));
            boost::hash_combine(seed, hash_posix_duration(object.time_));
            boost::hash_combine(seed, hash_posix_duration(object.duration_));
            return seed;
        }
    };
}

#endif //ROWS_VISIT_H
