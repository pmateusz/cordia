#include "service_user.h"

namespace rows {

    ServiceUser::ServiceUser()
            : ServiceUser("") {}

    ServiceUser::ServiceUser(std::string id)
            : id_(std::move(id)) {}

    ServiceUser::ServiceUser(const ServiceUser &other)
            : id_(other.id_) {}

    ServiceUser::ServiceUser(ServiceUser &&other) noexcept
            : id_(other.id_) {}

    ServiceUser &ServiceUser::operator=(const ServiceUser &other) {
        id_ = other.id_;
        return *this;
    }

    ServiceUser &ServiceUser::operator=(ServiceUser &&other) noexcept {
        id_ = std::move(other.id_);
        return *this;
    }

    std::ostream &operator<<(std::ostream &out, const ServiceUser &service_user) {
        out << service_user.id_;
        return out;
    }

    bool ServiceUser::operator==(const ServiceUser &other) const {
        return id_ == other.id_;
    }

    bool ServiceUser::operator!=(const ServiceUser &other) const {
        return !this->operator==(other);
    }

    const std::string &ServiceUser::id() const {
        return id_;
    }
}
