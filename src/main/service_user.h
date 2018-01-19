#ifndef ROWS_SERVICE_USER_H
#define ROWS_SERVICE_USER_H

#include <string>
#include <ostream>
#include <functional>

#include <boost/functional/hash.hpp>

namespace rows {

    class ServiceUser {
    public:
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
}

#endif //ROWS_SERVICE_USER_H
