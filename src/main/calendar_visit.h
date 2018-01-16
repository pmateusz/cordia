#ifndef ROWS_VISIT_H
#define ROWS_VISIT_H

#include <cstddef>
#include <ostream>
#include <functional>

#include <boost/optional.hpp>
#include <boost/optional/optional_io.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "location.h"
#include "address.h"
#include "service_user.h"
#include "data_time.h"

namespace rows {

    class CalendarVisit {
    public:
        CalendarVisit(ServiceUser service_user,
                      Address address,
                      boost::optional<Location> location,
                      boost::posix_time::ptime date_time,
                      boost::posix_time::time_duration duration);

        CalendarVisit(ServiceUser service_user,
                      Address address,
                      boost::posix_time::ptime date_time,
                      boost::posix_time::time_duration duration);

        CalendarVisit(const CalendarVisit &other);

        CalendarVisit &operator=(const CalendarVisit &other);

        friend struct std::hash<rows::CalendarVisit>;

        friend std::ostream &operator<<(std::ostream &out, const CalendarVisit &object);

        bool operator==(const CalendarVisit &other) const;

        bool operator!=(const CalendarVisit &other) const;

        void location(Location location);

        const boost::optional<Location> &location() const;

        const boost::posix_time::ptime datetime() const;

        const boost::posix_time::ptime::time_duration_type duration() const;

        class JsonLoader : protected rows::JsonLoader {
        public:
            template<typename JsonType>
            CalendarVisit Load(const JsonType &document) const;
        };

    private:
        ServiceUser service_user_;
        Address address_;
        boost::optional<Location> location_;
        boost::posix_time::ptime date_time_;
        boost::posix_time::ptime::time_duration_type duration_;
    };
}

namespace std {

    template<>
    struct hash<boost::posix_time::ptime::date_type> {
        typedef boost::posix_time::ptime::date_type argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<boost::gregorian::date::date_rep_type::int_type> hash{};
            return hash(object.day_count().as_number());
        }
    };

    template<>
    struct hash<boost::posix_time::ptime::time_duration_type> {
        typedef boost::posix_time::ptime::time_duration_type argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<boost::posix_time::ptime::time_duration_type::tick_type> hash{};
            return hash(object.get_rep().as_number());
        }
    };

    template<>
    struct hash<boost::posix_time::ptime> {
        typedef boost::posix_time::ptime argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<boost::posix_time::ptime::date_type> hash_date{};
            static const std::hash<boost::posix_time::ptime::time_duration_type> hash_time{};

            std::size_t seed = 0;
            boost::hash_combine(seed, hash_date(object.date()));
            boost::hash_combine(seed, hash_time(object.time_of_day()));
            return seed;
        }
    };

    template<>
    struct hash<rows::CalendarVisit> {
        typedef rows::CalendarVisit argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<rows::Location> hash_location{};
            static const std::hash<rows::Address> hash_address{};
            static const std::hash<rows::ServiceUser> hash_service_user{};
            static const std::hash<boost::posix_time::ptime> hash_date_time{};
            static const std::hash<boost::posix_time::ptime::time_duration_type> hash_duration{};

            std::size_t seed = 0;
            if (object.location_) {
                boost::hash_combine(seed, hash_location(object.location_.value()));
            }
            boost::hash_combine(seed, hash_address(object.address_));
            boost::hash_combine(seed, hash_service_user(object.service_user_));
            boost::hash_combine(seed, hash_date_time(object.date_time_));
            boost::hash_combine(seed, hash_duration(object.duration_));
            return seed;
        }
    };
}

namespace rows {

    template<typename JsonType>
    CalendarVisit CalendarVisit::JsonLoader::Load(const JsonType &document) const {
        static const Address::JsonLoader address_loader{};
        static const Location::JsonLoader location_loader{};
        static const DateTime::JsonLoader datetime_loader{};

        const auto datetime = datetime_loader.Load(document);
        const auto duration_it = document.find("duration");
        if (duration_it == std::end(document)) { throw OnKeyNotFound("duration"); }
        boost::posix_time::time_duration duration
                = boost::posix_time::seconds(std::stol(duration_it.value().template get<std::string>()));

        Address address;
        const auto address_it = document.find("address");
        if (address_it != std::end(document)) {
            address = address_loader.Load(address_it.value());
        }

        boost::optional<Location> location;
        const auto location_it = document.find("location");
        if (location_it != std::end(document)) {
            location = location_loader.Load(document);
        }

        ServiceUser service_user;
        const auto service_user_it = document.find("service_user");
        if (service_user_it != std::end(document)) {
            service_user = ServiceUser(service_user_it.value().template get<std::string>());
        }

        CalendarVisit visit(service_user, address, datetime, duration);
        if (location) {
            visit.location(location.value());
        }
        return visit;
    }
}

#endif //ROWS_VISIT_H