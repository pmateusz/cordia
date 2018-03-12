#ifndef ROWS_ADDRESS_H
#define ROWS_ADDRESS_H

#include <string>
#include <ostream>
#include <functional>

#include <boost/functional/hash.hpp>

namespace rows {

    class Address {
    public:
        static const Address DEFAULT;

        Address();

        Address(std::string house_number, std::string street, std::string city, std::string post_code);

        Address(const Address &other);

        Address(Address &&other) noexcept;

        Address &operator=(const Address &other);

        Address &operator=(Address &&other)noexcept;

        friend struct std::hash<rows::Address>;

        friend std::ostream &operator<<(std::ostream &out, const Address &object);

        bool operator==(const Address &other) const;

        bool operator!=(const Address &other) const;

        const std::string &house_number() const;

        const std::string &street() const;

        const std::string &city() const;

        const std::string &post_code() const;

        class JsonLoader {
        public:
            template<typename JsonType>
            Address Load(const JsonType &document) const;
        };

    private:
        std::string house_number_;
        std::string street_;
        std::string city_;
        std::string post_code_;
    };

    template<typename JsonType>
    Address Address::JsonLoader::Load(const JsonType &document) const {
        std::string street;
        const auto road_it = document.find("road");
        if (road_it != std::end(document)) {
            street = road_it.value().template get<std::string>();
        }

        std::string house_number;
        const auto house_number_it = document.find("house_number");
        if (house_number_it != std::end(document)) {
            house_number = house_number_it.value().template get<std::string>();
        }

        std::string city;
        const auto city_it = document.find("city");
        if (city_it != std::end(document)) {
            city = city_it.value().template get<std::string>();
        }

        std::string post_code;
        const auto post_code_it = document.find("post_code");
        if (post_code_it != std::end(document)) {
            post_code = post_code_it.value().template get<std::string>();
        }

        return {house_number, street, city, post_code};
    }
}

namespace std {

    template<>
    struct hash<rows::Address> {
        typedef rows::Address argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<string> hash_string{};

            std::size_t seed = 0;
            boost::hash_combine(seed, hash_string(object.house_number_));
            boost::hash_combine(seed, hash_string(object.street_));
            boost::hash_combine(seed, hash_string(object.post_code_));
            boost::hash_combine(seed, hash_string(object.city_));
            return seed;
        }
    };
}


#endif //ROWS_ADDRESS_H
