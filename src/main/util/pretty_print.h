#ifndef ROWS_PRETTY_PRINT_H
#define ROWS_PRETTY_PRINT_H

#include <string>

#include <osrm/util/coordinate.hpp>

namespace util {

    std::string to_simple_string(const osrm::util::FloatLatitude &value);

    std::string to_simple_string(const osrm::util::FloatLongitude &value);
}


#endif //ROWS_PRETTY_PRINT_H
