#ifndef ROWS_HASH_H
#define ROWS_HASH_H

#include <functional>
#include <boost/functional/hash.hpp>
#include <boost/date_time.hpp>

namespace std {

    template<>
    struct hash<boost::gregorian::date> {
        std::size_t operator()(const boost::gregorian::date &date) const noexcept {
            if (!date.is_special()) {
                std::size_t seed = 0;
                boost::hash_combine(seed, date.day().as_number());
                boost::hash_combine(seed, date.month().as_number());
                boost::hash_combine(seed, static_cast<unsigned short>(date.year()));
                return seed;
            }

            return date.as_special();
        }
    };

    template<>
    struct hash<boost::posix_time::time_duration> {
        std::size_t operator()(const boost::posix_time::time_duration &time_duration) const noexcept {
            return time_duration.total_nanoseconds();
        }
    };

    template<>
    struct hash<boost::posix_time::ptime> {
        std::size_t operator()(const boost::posix_time::ptime &datetime) const noexcept {
            static const std::hash<boost::gregorian::date> date_hasher;
            static const std::hash<boost::posix_time::time_duration> time_duration_hasher;

            std::size_t seed = 0;
            boost::hash_combine(seed, date_hasher(datetime.date()));
            boost::hash_combine(seed, time_duration_hasher(datetime.time_of_day()));
            return seed;
        }
    };

    template<>
    struct hash<boost::posix_time::time_period> {
        std::size_t operator()(const boost::posix_time::time_period &time_period) const noexcept {
            static const std::hash<boost::posix_time::ptime> ptime_hasher;

            std::size_t seed = 0;
            boost::hash_combine(seed, ptime_hasher(time_period.begin()));
            boost::hash_combine(seed, ptime_hasher(time_period.end()));
            return seed;
        }
    };
}
#endif //ROWS_HASH_H
