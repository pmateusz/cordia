#include "solution.h"

#include <boost/format.hpp>

std::domain_error rows::Solution::JsonLoader::OnKeyNotFound(std::string key) {
    return std::domain_error((boost::format("Key '%1%' not found") % key).str());
}