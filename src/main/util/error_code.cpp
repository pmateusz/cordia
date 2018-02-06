#include "error_code.h"

int util::to_exit_code(util::ErrorCode error_code) {
    switch (error_code) {
        case ErrorCode::UNKNOWN:
        case ErrorCode::OK:
            return 0;
        case ErrorCode::ERROR:
            return 1;
    }
}
