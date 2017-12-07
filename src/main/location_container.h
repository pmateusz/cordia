#ifndef ROWS_LOCATION_CONTAINER_H
#define ROWS_LOCATION_CONTAINER_H

#include <cstddef>
#include <iterator>
#include <vector>
#include <unordered_map>

#include <osrm/osrm.hpp>

#include <ortools/constraint_solver/routing.h>
#include <osrm/util/coordinate.hpp>

#include "location.h"

namespace rows {

    class LocationContainer {
    public:
        explicit LocationContainer(osrm::EngineConfig &config);

        /*!
         * @return transfer time in seconds
         */
        int64 Distance(const Location &from, const Location &to) const;

    private:
        static osrm::util::Coordinate ToCoordinate(const Location &location);

        osrm::OSRM routing_service_;
    };

    class CachedLocationContainer {
    public:
        template<typename LocationIteratorType>
        CachedLocationContainer(const LocationIteratorType &begin_it,
                                const LocationIteratorType &end_it,
                                osrm::EngineConfig &config);

        int64 Distance(const Location &from, const Location &to);

    private:
        template<typename LocationIteratorType>
        CachedLocationContainer(const LocationIteratorType &begin_it,
                                const LocationIteratorType &end_it,
                                std::size_t distance,
                                osrm::EngineConfig &config);

        std::unordered_map<Location, std::size_t> location_index_;
        std::vector<std::vector<int64> > distance_matrix_;
        LocationContainer location_container_;
    };
}

namespace rows {
    template<typename LocationIteratorType>
    CachedLocationContainer::CachedLocationContainer(const LocationIteratorType &begin_it,
                                                     const LocationIteratorType &end_it,
                                                     osrm::EngineConfig &config)
            : CachedLocationContainer(begin_it,
                                      end_it,
                                      static_cast<std::size_t>(std::distance(begin_it, end_it)),
                                      config) {}

    template<typename LocationIteratorType>
    CachedLocationContainer::CachedLocationContainer(const LocationIteratorType &begin_it,
                                                     const LocationIteratorType &end_it,
                                                     std::size_t distance,
                                                     osrm::EngineConfig &config)
            : location_index_(),
              distance_matrix_(distance, std::vector<int64>(distance, -1)),
              location_container_(config) {
        std::size_t index = 0;
        for (auto location_it = begin_it; location_it != end_it; ++location_it, ++index) {
            location_index_.insert(std::make_pair(*location_it, index));
        }
    }
}

#endif //ROWS_LOCATION_CONTAINER_H
