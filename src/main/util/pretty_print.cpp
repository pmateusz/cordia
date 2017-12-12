#include "pretty_print.h"

namespace util {

    std::string to_simple_string(const osrm::util::FloatLatitude &value) {
        return std::to_string(static_cast<double>(value));
    }

    std::string to_simple_string(const osrm::util::FloatLongitude &value) {
        return std::to_string(static_cast<double>(value));
    }
}
