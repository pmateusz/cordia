#ifndef ROWS_APLICATION_ERROR_H
#define ROWS_APLICATION_ERROR_H

#include <string>
#include <exception>

namespace util {

    class ApplicationError : public std::exception {
    public:
        ApplicationError(std::string msg, std::string diagnostic_info, int exit_code);

        ApplicationError(std::string msg, int exit_code);

        const char * what () const throw ();

        inline std::string msg() const { return msg_; }

        inline std::string diagnostic_info() const { return diagnostic_info_; }

        inline int exit_code() const { return exit_code_; }

    private:
        std::string msg_;
        std::string diagnostic_info_;
        int exit_code_;
    };
}


#endif //ROWS_APLICATION_ERROR_H
