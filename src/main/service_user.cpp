#include <boost/format.hpp>

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

    ExtendedServiceUser::ExtendedServiceUser()
            : ExtendedServiceUser("", Address(), Location(0, 0), std::unordered_map<Carer, double>()) {}

    ExtendedServiceUser::ExtendedServiceUser(std::string id,
                                             Address address,
                                             Location location,
                                             std::unordered_map<Carer, double> carer_preference)
            : ServiceUser(std::move(id)),
              location_(std::move(location)),
              address_(std::move(address)),
              carer_preference_(std::move(carer_preference)) {}

    ExtendedServiceUser::ExtendedServiceUser(const ExtendedServiceUser &other)
            : ServiceUser(other),
              location_(other.location_),
              address_(other.address_),
              carer_preference_(other.carer_preference_) {}

    ExtendedServiceUser::ExtendedServiceUser(ExtendedServiceUser &&other) noexcept
            : ServiceUser(std::move(other)),
              location_(std::move(other.location_)),
              address_(std::move(other.address_)),
              carer_preference_(other.carer_preference_) {}

    ExtendedServiceUser &ExtendedServiceUser::operator=(const ExtendedServiceUser &other) {
        this->ServiceUser::operator=(other);
        location_ = other.location_;
        address_ = other.address_;
        carer_preference_ = other.carer_preference_;
        return *this;
    }

    ExtendedServiceUser &ExtendedServiceUser::operator=(ExtendedServiceUser &&other) noexcept {
        this->ServiceUser::operator=(other);
        location_ = std::move(other.location_);
        address_ = std::move(other.address_);
        carer_preference_ = std::move(other.carer_preference_);
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
               && location_ == other.location_
               && carer_preference_ == other.carer_preference_;
    }

    bool ExtendedServiceUser::operator!=(const ExtendedServiceUser &other) const {
        return this->ServiceUser::operator!=(other)
               || address_ != other.address_
               || location_ != other.location_
               || carer_preference_ != other.carer_preference_;
    }

    const Address &ExtendedServiceUser::address() const {
        return address_;
    }

    const Location &ExtendedServiceUser::location() const {
        return location_;
    }

    double ExtendedServiceUser::preference(const Carer &carer) const {
        const auto carer_preference_it = carer_preference_.find(carer);
        if (carer_preference_it == std::end(carer_preference_)) {
            return 0.0;
        }
        return carer_preference_it->second;
    }
}
