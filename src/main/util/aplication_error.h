#ifndef ROWS_APLICATION_ERROR_H
#define ROWS_APLICATION_ERROR_H

#include <string>
#include <exception>

#include "error_code.h"

namespace util {

    class ApplicationError : public std::exception {
    public:
        ApplicationError(std::string msg, std::string diagnostic_info, ErrorCode error_code);

        ApplicationError(std::string msg, ErrorCode exit_code);

        const char *what() const throw();

        inline std::string msg() const { return msg_; }

        inline std::string diagnostic_info() const { return diagnostic_info_; }

        inline ErrorCode error_code() const { return error_code_; }

    private:
        std::string msg_;
        std::string diagnostic_info_;
        ErrorCode error_code_;
    };
}


#endif //ROWS_APLICATION_ERROR_H
