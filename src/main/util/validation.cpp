#include "validation.h"

#include <glog/logging.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>

bool util::ValidateFilePath(const char *flagname, const std::string &value) {
    boost::filesystem::path file_path(value);
    if (!boost::filesystem::exists(file_path)) {
        LOG(ERROR) << boost::format("File '%1%' does not exist") % file_path;
        return false;
    }

    if (!boost::filesystem::is_regular_file(file_path)) {
        LOG(ERROR) << boost::format("Path '%1%' does not point to a file") % file_path;
        return false;
    }

    return true;
}

bool util::TryValidateFilePath(const char *flagname, const std::string &value) {
    if (value.empty()) {
        return true;
    }

    return ValidateFilePath(flagname, value);
}
