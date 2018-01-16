#ifndef ROWS_JSON_H
#define ROWS_JSON_H

#include <string>
#include <stdexcept>

#include <boost/format.hpp>

namespace rows {

    class JsonLoader {

    protected:
        std::domain_error OnKeyNotFound(std::string key) const;
    };
}


#endif //ROWS_JSON_H
