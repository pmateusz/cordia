#ifndef ROWS_VALIDATION_H
#define ROWS_VALIDATION_H

#include <string>

namespace util {

    bool ValidateFilePath(const char *flagname, const std::string &value);

    bool TryValidateFilePath(const char *flagname, const std::string &value);
}


#endif //ROWS_VALIDATION_H
