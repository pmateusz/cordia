#include "json.h"

std::domain_error rows::JsonLoader::OnKeyNotFound(std::string key) const {
    return std::domain_error((boost::format("Key '%1%' not found") % key).str());
}
