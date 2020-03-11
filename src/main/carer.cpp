#include "carer.h"

#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <util/aplication_error.h>

namespace rows {

    Carer::Carer()
            : Carer("", Transport::Foot, std::vector<int>()) {}

    Carer::Carer(std::string sap_number)
            : Carer(std::move(sap_number), Transport::Foot, std::vector<int>()) {}

    Carer::Carer(std::string sap_number, Transport transport, std::vector<int> skills)
            : sap_number_(std::move(sap_number)),
              transport_(transport),
              skills_(std::move(skills)) {}

    Carer::Carer(const Carer &other)
            : sap_number_(other.sap_number_),
              transport_(other.transport_),
              skills_(other.skills_) {}

    Carer::Carer(Carer &&other) noexcept
            : sap_number_(std::move(other.sap_number_)),
              transport_(other.transport_),
              skills_(std::move(other.skills_)) {}

    Carer &Carer::operator=(const Carer &other) {
        sap_number_ = other.sap_number_;
        transport_ = other.transport_;
        skills_ = other.skills_;
        return *this;
    }

    Carer &Carer::operator=(Carer &&other) noexcept {
        sap_number_ = std::move(other.sap_number_);
        transport_ = other.transport_;
        skills_ = std::move(other.skills_);
        return *this;
    }

    bool Carer::operator==(const Carer &other) const {
        return sap_number_ == other.sap_number_ && transport_ == other.transport_ && skills_ == other.skills_;
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

    const std::vector<int> &Carer::skills() const {
        return skills_;
    }

    std::ostream &operator<<(std::ostream &out, const Carer &object) {
        out << boost::format("(%1%)") % object.sap_number_;
        return out;
    }

    bool Carer::has_skills(const std::vector<int> &skills) const {
        for (const auto skill : skills) {
            if (std::find(std::cbegin(skills_), std::cend(skills_), skill) == std::cend(skills_)) {
                return false;
            }
        }
        return true;
    }

    std::vector<int> Carer::shared_skills(const std::vector<int> &skills) const {
        std::vector<int> shared_skills;
        for (const auto skill : skills) {
            if (std::find(std::cbegin(skills_), std::cend(skills_), skill) != std::cend(skills_)) {
                shared_skills.push_back(skill);
            }
        }
        return shared_skills;
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

    void from_json(const nlohmann::json &json, Carer &carer) {
        const auto sap_number = json.at("sap_number").get<std::string>();
        const auto transport = ParseTransport(json.at("mobility").get<std::string>());

        Carer json_carer{sap_number, transport, {}};
        carer = std::move(json_carer);
    }
}