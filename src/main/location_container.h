#ifndef ROWS_LOCATION_CONTAINER_H
#define ROWS_LOCATION_CONTAINER_H

#include <vector>

#include <ortools/constraint_solver/routing.h>

#include <location.h>

namespace rows {

    class LocationContainer {
    public:
        bool Add(const Location &value);

        int64 Distance(const Location &from, const Location &to) const;

    private:
        std::vector<Location> locations_;
    };
}


#endif //ROWS_LOCATION_CONTAINER_H
