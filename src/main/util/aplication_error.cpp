#include "aplication_error.h"

util::ApplicationError::ApplicationError(std::string msg, std::string diagnostic_info, ErrorCode error_code)
        : msg_(std::move(msg)),
          diagnostic_info_(std::move(diagnostic_info)),
          error_code_(error_code) {}

util::ApplicationError::ApplicationError(std::string msg, ErrorCode error_code)
        : ApplicationError(std::move(msg), nullptr, error_code) {}

const char *util::ApplicationError::what() const throw() {
    return msg_.c_str();
}
