#include <regex>

#include <glog/logging.h>

#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/date_time.hpp>

#include "validation.h"

bool util::file::Exists(const char *flagname, const std::string &value) {
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

bool util::file::IsNullOrExists(const char *flagname, const std::string &value) {
    if (value.empty()) {
        return true;
    }

    return Exists(flagname, value);
}

bool util::file::IsNullOrNotExists(const char *flagname, const std::string &value) {
    if (value.empty()) {
        return true;
    }

    boost::filesystem::path file_path(value);
    if (boost::filesystem::exists(file_path)) {
        LOG(ERROR) << boost::format("File %1% already exists") % file_path;
        return false;
    }

    return true;
}

std::string util::file::GenerateNewFilePath(const std::string &pattern) {
    static const std::regex FILE_BASE_NAME_VERSION_PATTERN{"^(.*?)_version(\\d+)$", std::regex_constants::icase};

    boost::filesystem::path file_path{pattern};
    if (!boost::filesystem::exists(file_path)) {
        return file_path.string();
    }

    std::string stem;
    auto current_version = 0ul;
    std::string extension = boost::filesystem::extension(file_path);
    const auto root_dir = file_path.root_directory();

    std::cmatch version_match;
    if (std::regex_match(file_path.c_str(), version_match, FILE_BASE_NAME_VERSION_PATTERN)) {
        stem = version_match[0];
        current_version = std::stoul(version_match[1]);
    } else {
        stem = file_path.stem().string();
        current_version = 0;
    }

    while (boost::filesystem::exists(file_path)) {
        ++current_version;

        std::stringstream raw_file_path;
        raw_file_path << stem << "_version" << std::to_string(current_version);
        if (!extension.empty()) {
            raw_file_path << extension;
        }

        file_path = root_dir / raw_file_path.str();
    }

    return file_path.string();
}

bool util::date_time::IsPositive(const char *flagname, const std::string &value) {
    const auto duration = boost::posix_time::duration_from_string(value);
    if (duration.is_negative() || duration.total_seconds() <= 0) {
        LOG(ERROR) << boost::format("Duration %1% is not positive")
                      % duration;
        return false;
    }

    return true;
}

bool util::date_time::IsNullOrPositive(const char *flagname, const std::string &value) {
    if (value.empty()) {
        return true;
    }

    return IsPositive(flagname, value);
}

void util::string::Strip(std::string &text) {
    static const std::regex NON_PRINTABLE_CHARACTER_PATTERN{"[\\W]"};
    text = std::regex_replace(text, NON_PRINTABLE_CHARACTER_PATTERN, "");
}

void util::string::ToLower(std::string &text) {
    static const std::locale CURRENT_LOCALE("");

    std::transform(std::begin(text), std::end(text), std::begin(text),
                   [](auto character) {
                       return std::tolower(character, CURRENT_LOCALE);
                   });
}
