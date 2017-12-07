#include "location_container.h"

#include <string>
#include <cmath>
#include <ostream>

#include <osrm/match_parameters.hpp>
#include <osrm/nearest_parameters.hpp>
#include <osrm/route_parameters.hpp>
#include <osrm/table_parameters.hpp>
#include <osrm/trip_parameters.hpp>

#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <mapbox/variant_io.hpp>

#include <osrm/osrm.hpp>

#include <boost/format.hpp>
#include <boost/exception/diagnostic_information.hpp>

#include <glog/logging.h>

namespace osrm {

    namespace util {

        namespace json {

//            std::ostream &operator<<(std::ostream &out, const osrm::util::json::String &value) {
//                out << value.value;
//                return out;
//            }
//
//            std::ostream &operator<<(std::ostream &out, const osrm::util::json::Number &value) {
//                out << value.value;
//                return out;
//            }

            std::ostream &operator<<(std::ostream &out, const Value &value) {
                return out;
            }

            std::ostream &operator<<(std::ostream &out, const std::vector<Value> &value) {
                return out;
            }

//            std::ostream &operator<<(std::ostream &out, const osrm::util::json::Array &value) {
//                out << value.values;
//                return out;
//            }

//            std::ostream &operator<<(std::ostream &out, const osrm::json::Object &object) {
//                std::stringstream stream;
//
//                stream << "{ ";
//                for (const auto &pair : object.values) {
//                    stream << '"' << pair.first << "': " << pair.second << " ";
//                }
//                stream << "}";
//
//                out << stream.str();
//                return out;
//            }
        }
    }
}

namespace rows {

    LocationContainer::LocationContainer(osrm::EngineConfig &config)
            : routing_service_(config) {}

    // TODO: handle errors
    int64 LocationContainer::Distance(const Location &from, const Location &to) const {
        static const auto INFINITE_DISTANCE = std::numeric_limits<int64>::max();

        if (from == to) {
            return 0;
        }

        osrm::RouteParameters params;
        params.coordinates = {ToCoordinate(from), ToCoordinate(to)};

        DCHECK(params.IsValid());

        osrm::json::Object result;

        try {
            const auto status = routing_service_.Route(params, result);
            if (status == osrm::Status::Ok) {
                auto routes_it = result.values.find("routes");
                if (routes_it == std::end(result.values)) {
                    LOG(ERROR) << boost::format("No routes have been found from '%1%' to '%2%'")
                                  % from
                                  % to;
                    return INFINITE_DISTANCE;
                }

                auto &routes = routes_it->second.get<osrm::json::Array>();
                if (routes.values.empty()) {
                    LOG(ERROR) << boost::format("No routes have been found from '%1%' to '%2%'")
                                  % from
                                  % to;
                    return INFINITE_DISTANCE;
                }

                auto &route = routes.values.at(0).get<osrm::json::Object>();
                auto duration_it = route.values.find("duration");
                if (duration_it == std::end(route.values)) {
                    LOG(ERROR) << boost::format("Duration have been calculated for a route found from '%1%' to '%2%'")
                                  % from
                                  % to;
                    return INFINITE_DISTANCE;
                }

                const auto duration = duration_it->second.get<osrm::json::Number>().value;
                return static_cast<int64>(std::ceil(duration));
            }

            std::stringstream msg;
            msg << result;
            LOG(ERROR) << boost::format("Failed to find a route from '%1%' to '%2%' due to error: %3%")
                          % from
                          % to
                          % msg.str();
            return INFINITE_DISTANCE;
        } catch (...) {
            std::stringstream msg;
            msg << result;
            LOG(ERROR) << boost::format("Failed to calculate distance from '%1%' to '%2%' due to error: %3%\n%4%")
                          % from
                          % to
                          % boost::current_exception_diagnostic_information()
                          % msg.str();
            return INFINITE_DISTANCE;
        }
    }

    osrm::util::Coordinate LocationContainer::ToCoordinate(const Location &location) {
        const osrm::util::FloatLatitude latitude{std::stod(location.latitude())};
        const osrm::util::FloatLongitude longitude{std::stod(location.longitude())};
        return {longitude, latitude};
    }

    int64 CachedLocationContainer::Distance(const Location &from, const Location &to) {
        const auto from_it = location_index_.find(from);
        DCHECK(from_it != std::end(location_index_));

        const auto to_it = location_index_.find(to);
        DCHECK(to_it != std::end(location_index_));

        const auto cached_distance = distance_matrix_[from_it->second][to_it->second];
        if (cached_distance >= 0) {
            return cached_distance;
        }

        const auto distance = location_container_.Distance(from, to);
        DCHECK_GE(distance, 0);

        distance_matrix_[from_it->second][to_it->second] = distance;

        return distance;
    }
}
