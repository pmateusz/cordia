#ifndef ROWS_CARER_H
#define ROWS_CARER_H

#include <string>

#include <boost/functional/hash.hpp>

namespace rows {

    enum class Transport {
        Unknown, Foot, Car
    };

    Transport ParseTransport(const std::string &value);

    class Carer {
    public:
        Carer();

        explicit Carer(std::string sap_number);

        Carer(std::string sap_number, Transport transport, std::vector<int> skills);

        Carer(const Carer &other);

        Carer(Carer &&other) noexcept;

        Carer &operator=(const Carer &other);

        Carer &operator=(Carer &&other) noexcept;

        friend struct std::hash<rows::Carer>;

        friend std::ostream &operator<<(std::ostream &out, const Carer &object);

        bool operator==(const Carer &other) const;

        bool operator!=(const Carer &other) const;

        const std::string &sap_number() const;

        Transport transport() const;

        bool has_skills(const std::vector<int> &skills) const;

        std::vector<int> shared_skills(const std::vector<int> &skills) const;

        const std::vector<int> &skills() const;

    private:
        std::string sap_number_;
        Transport transport_;
        std::vector<int> skills_;
    };
}

namespace std {

    template<>
    struct hash<rows::Carer> {
        typedef rows::Carer argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            std::size_t seed = 0;
            boost::hash_combine(seed, object.sap_number_);
            return seed;
        }
    };
}

#endif //ROWS_CARER_H
