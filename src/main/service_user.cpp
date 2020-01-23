#include <boost/format.hpp>

#include "service_user.h"

namespace rows {

    const ServiceUser ServiceUser::DEFAULT{};

    ServiceUser::ServiceUser()
            : ServiceUser(0) {}

    ServiceUser::ServiceUser(long id)
            : id_(id) {}

    ServiceUser::ServiceUser(const ServiceUser &other)
            : id_(other.id_) {}

    ServiceUser::ServiceUser(ServiceUser &&other) noexcept
            : id_(other.id_) {}

    ServiceUser &ServiceUser::operator=(const ServiceUser &other) {
        id_ = other.id_;
        return *this;
    }

    ServiceUser &ServiceUser::operator=(ServiceUser &&other) noexcept {
        id_ = other.id_;
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

    long ServiceUser::id() const {
        return id_;
    }

    ExtendedServiceUser::ExtendedServiceUser()
            : ExtendedServiceUser(0, Address(), Location("", "")) {}

    ExtendedServiceUser::ExtendedServiceUser(long id,
                                             Address address,
                                             Location location)
            : ServiceUser(id),
              location_(std::move(location)),
              address_(std::move(address)) {}

    ExtendedServiceUser::ExtendedServiceUser(const ExtendedServiceUser &other)
            : ServiceUser(other),
              location_(other.location_),
              address_(other.address_) {}

    ExtendedServiceUser::ExtendedServiceUser(ExtendedServiceUser &&other) noexcept
            : ServiceUser(std::move(other)),
              location_(std::move(other.location_)),
              address_(std::move(other.address_)) {}

    ExtendedServiceUser &ExtendedServiceUser::operator=(const ExtendedServiceUser &other) {
        this->ServiceUser::operator=(other);
        location_ = other.location_;
        address_ = other.address_;
        return *this;
    }

    ExtendedServiceUser &ExtendedServiceUser::operator=(ExtendedServiceUser &&other) noexcept {
        this->ServiceUser::operator=(other);
        location_ = std::move(other.location_);
        address_ = std::move(other.address_);
        return *this;
    }

    std::ostream &operator<<(std::ostream &out, const ExtendedServiceUser &service_user) {
        out << (boost::format("(%1%, %2%, %3%)")
                % service_user.id()
                % service_user.address_
                % service_user.location_).str();
        return out;
    }

    bool ExtendedServiceUser::operator==(const ExtendedServiceUser &other) const {
        return this->ServiceUser::operator==(other)
               && address_ == other.address_
               && location_ == other.location_;
    }

    bool ExtendedServiceUser::operator!=(const ExtendedServiceUser &other) const {
        return this->ServiceUser::operator!=(other)
               || address_ != other.address_
               || location_ != other.location_;
    }

    const Address &ExtendedServiceUser::address() const {
        return address_;
    }

    const Location &ExtendedServiceUser::location() const {
        return location_;
    }
}
