#include "address.h"

namespace rows {

    Address::Address()
            : Address("", "", "", "") {}

    Address::Address(std::string house_number, std::string street, std::string city, std::string post_code)
            : house_number_(std::move(house_number)),
              street_(std::move(street)),
              city_(std::move(city)),
              post_code_(std::move(post_code)) {}

    Address::Address(const Address &other)
            : house_number_(other.house_number_),
              street_(other.street_),
              post_code_(other.post_code_),
              city_(other.city_) {}

    Address::Address(Address &&other) noexcept {
        house_number_ = std::move(other.house_number_);
        street_ = std::move(other.street_);
        post_code_ = std::move(other.post_code_);
        city_ = std::move(other.city_);
    }

    Address &Address::operator=(const Address &other) {
        house_number_ = other.house_number_;
        street_ = other.street_;
        post_code_ = other.post_code_;
        city_ = other.city_;
        return *this;
    }

    Address &Address::operator=(Address &&other) noexcept {
        house_number_ = std::move(other.house_number_);
        street_ = std::move(other.street_);
        post_code_ = std::move(other.post_code_);
        city_ = std::move(other.city_);
        return *this;
    }

    std::ostream &operator<<(std::ostream &out, const Address &address) {
        out << "(" << address.house_number()
            << ", " << address.street()
            << ", " << address.city()
            << ", " << address.post_code()
            << ")";
        return out;
    }

    bool Address::operator==(const Address &other) const {
        return house_number_ == other.house_number_
               && street_ == other.street_
               && city_ == other.city_
               && post_code_ == other.post_code_;
    }

    bool Address::operator!=(const Address &other) const {
        return !this->operator==(other);
    }

    const std::string &Address::house_number() const {
        return house_number_;
    }

    const std::string &Address::street() const {
        return street_;
    }

    const std::string &Address::city() const {
        return city_;
    }

    const std::string &Address::post_code() const {
        return post_code_;
    }
}
