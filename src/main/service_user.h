#ifndef ROWS_SERVICE_USER_H
#define ROWS_SERVICE_USER_H

#include <string>
#include <ostream>
#include <unordered_map>
#include <functional>

#include <boost/functional/hash.hpp>

#include "carer.h"
#include "address.h"
#include "location.h"

namespace rows {

    class ServiceUser {
    public:
        static const ServiceUser DEFAULT;

        ServiceUser();

        explicit ServiceUser(std::string id);

        ServiceUser(const ServiceUser &other);

        ServiceUser(ServiceUser &&other) noexcept;

        ServiceUser &operator=(const ServiceUser &other);

        ServiceUser &operator=(ServiceUser &&other) noexcept;

        friend struct std::hash<rows::ServiceUser>;

        friend std::ostream &operator<<(std::ostream &out, const ServiceUser &service_user);

        bool operator==(const ServiceUser &other) const;

        bool operator!=(const ServiceUser &other) const;

        const std::string &id() const;

    private:
        std::string id_;
    };

    class ExtendedServiceUser : public ServiceUser {
    public:
        ExtendedServiceUser();

        ExtendedServiceUser(std::string id,
                            Address address,
                            Location location);

        ExtendedServiceUser(const ExtendedServiceUser &other);

        ExtendedServiceUser(ExtendedServiceUser &&other) noexcept;

        ExtendedServiceUser &operator=(const ExtendedServiceUser &other);

        ExtendedServiceUser &operator=(ExtendedServiceUser &&other) noexcept;

        friend struct std::hash<rows::ExtendedServiceUser>;

        friend std::ostream &operator<<(std::ostream &out, const ExtendedServiceUser &service_user);

        bool operator==(const ExtendedServiceUser &other) const;

        bool operator!=(const ExtendedServiceUser &other) const;

        const Address &address() const;

        const Location &location() const;

    private:
        Address address_;
        Location location_;
    };
}

namespace std {

    template<>
    struct hash<rows::ServiceUser> {
        typedef rows::ServiceUser argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<std::string> hash_id{};
            return hash_id(object.id_);
        }
    };

    template<>
    struct hash<rows::ExtendedServiceUser> {
        typedef rows::ExtendedServiceUser argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<rows::ServiceUser> hash_service_user{};
            static const std::hash<rows::Address> hash_address{};
            static const std::hash<rows::Location> hash_location{};

            auto seed = hash_service_user(object);
            boost::hash_combine(seed, hash_address(object.address()));
            boost::hash_combine(seed, hash_location(object.location()));
            return seed;
        }
    };
}

#endif //ROWS_SERVICE_USER_H
