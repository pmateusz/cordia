#ifndef ROWS_VALIDATION_H
#define ROWS_VALIDATION_H

#include <string>

#include <boost/format.hpp>

namespace util {

    namespace file {

        bool Exists(const char *flagname, const std::string &value);

        bool IsNullOrExists(const char *flagname, const std::string &value);

        bool IsNullOrNotExists(const char *flagname, const std::string &value);

        std::string GenerateNewFilePath(const std::string &pattern);
    }

    namespace numeric {

        template<typename Number>
        bool IsPositive(const char *flagname, Number value);
    }

    namespace time_duration {

        bool IsNullOrPositive(const char *flagname, const std::string &value);

        bool IsPositive(const char *flagname, const std::string &value);
    }

    namespace date {

        bool IsNullOrPositive(const char *flagname, const std::string &value);

        bool IsPositive(const char *flagname, const std::string &value);
    }
}

namespace util::numeric {

    template<typename Number>
    bool IsPositive(const char *flagname, Number value) {
        if (value > 0) {
            return true;
        }

        LOG(ERROR) << boost::format("Number %1% is not positive") % value;
        return false;
    }
}

#endif //ROWS_VALIDATION_H
