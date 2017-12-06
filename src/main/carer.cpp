#include "carer.h"

#include <boost/format.hpp>

namespace rows {

    Carer::Carer(std::string sap_number)
            : sap_number_(std::move(sap_number)) {}

    Carer::Carer(const Carer &other)
            : sap_number_(other.sap_number_) {}

    Carer::Carer(Carer &&other) noexcept
            : sap_number_(std::move(other.sap_number_)) {}

    Carer &Carer::operator=(const Carer &other) {
        sap_number_ = other.sap_number_;
        return *this;
    }

    Carer &Carer::operator=(Carer &&other) noexcept {
        sap_number_ = std::move(other.sap_number_);
        return *this;
    }

    bool Carer::operator==(const Carer &other) {
        return sap_number_ == other.sap_number_;
    }

    bool Carer::operator!=(const Carer &other) {
        return !operator==(other);
    }

    const std::string &Carer::sap_number() const {
        return sap_number_;
    }

    std::ostream &operator<<(std::ostream &out, const Carer &object) {
        out << boost::format("(%1%)") % object.sap_number_;
        return out;
    }
}