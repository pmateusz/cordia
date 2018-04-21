#include "carer.h"

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <util/aplication_error.h>

namespace rows {

    Carer::Carer()
            : Carer("", Transport::Foot) {}

    Carer::Carer(std::string sap_number)
            : Carer(std::move(sap_number), Transport::Foot) {}

    Carer::Carer(std::string sap_number, Transport transport)
            : sap_number_(std::move(sap_number)),
              transport_(transport) {}

    Carer::Carer(const Carer &other)
            : sap_number_(other.sap_number_),
              transport_(other.transport_) {}

    Carer::Carer(Carer &&other) noexcept
            : sap_number_(std::move(other.sap_number_)),
              transport_(other.transport_) {}

    Carer &Carer::operator=(const Carer &other) {
        sap_number_ = other.sap_number_;
        transport_ = other.transport_;
        return *this;
    }

    Carer &Carer::operator=(Carer &&other) noexcept {
        sap_number_ = std::move(other.sap_number_);
        transport_ = other.transport_;
        return *this;
    }

    bool Carer::operator==(const Carer &other) const {
        return sap_number_ == other.sap_number_ && transport_ == other.transport_;
    }

    bool Carer::operator!=(const Carer &other) const {
        return !operator==(other);
    }

    const std::string &Carer::sap_number() const {
        return sap_number_;
    }

    Transport Carer::transport() const {
        return transport_;
    }

    std::ostream &operator<<(std::ostream &out, const Carer &object) {
        out << boost::format("(%1%)") % object.sap_number_;
        return out;
    }

    Transport ParseTransport(const std::string &value) {
        if (value.empty()) {
            return Transport::Unknown;
        }

        const auto value_to_use = boost::algorithm::to_lower_copy(value);
        if (value_to_use == "foot") {
            return Transport::Foot;
        } else if (value_to_use == "car") {
            return Transport::Car;
        }

        throw util::ApplicationError((boost::format("Unknown value of Transport: %1%. Use either 'foot' or 'car'.")
                                      % value).str(), util::ErrorCode::ERROR);
    }
}