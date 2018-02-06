#ifndef ROWS_ERROR_CODES_H
#define ROWS_ERROR_CODES_H

namespace util {

    enum class ErrorCode {
        UNKNOWN,
        OK,
        ERROR
    };

    int to_exit_code(ErrorCode error_code);
}


#endif //ROWS_ERROR_CODES_H
