#include "aplication_error.h"

util::ApplicationError::ApplicationError(std::string msg, std::string diagnostic_info, int exit_code)
        : msg_(std::move(msg)),
          diagnostic_info_(std::move(diagnostic_info)),
          exit_code_(exit_code) {}

util::ApplicationError::ApplicationError(std::string msg, int exit_code)
        : ApplicationError(std::move(msg), nullptr, exit_code) {}

const char *util::ApplicationError::what() const throw() {
    return msg_.c_str();
}
