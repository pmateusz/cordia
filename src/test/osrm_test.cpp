#include "osrm/match_parameters.hpp"
#include "osrm/nearest_parameters.hpp"
#include "osrm/route_parameters.hpp"
#include "osrm/table_parameters.hpp"
#include "osrm/trip_parameters.hpp"

#include "osrm/coordinate.hpp"
#include "osrm/engine_config.hpp"
#include "osrm/json_container.hpp"
#include "osrm/storage_config.hpp"
#include "osrm/osrm.hpp"

#include <iostream>
#include <string>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <boost/algorithm/string/join.hpp>

#include "util/logging.h"

TEST(TestOSRM, CanCalculateTravelTime) {
    // Configure based on a .osrm base path, and no datasets in shared mem from osrm-datastore
    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig("../data/scotland-latest.osrm");
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    EXPECT_TRUE(config.IsValid());

    // Routing machine with several services (such as Route, Table, Nearest, Trip, Match)
    const osrm::OSRM osrm{config};

    // The following shows how to use the Route service; configure this service
    osrm::RouteParameters params;
    // a route in Glasgow
    params.coordinates.push_back({osrm::util::FloatLongitude{-4.267129}, osrm::util::FloatLatitude{55.8659861}});
    params.coordinates.push_back({osrm::util::FloatLongitude{-4.245461}, osrm::util::FloatLatitude{55.862235}});

    // Response is in JSON format
    osrm::json::Object result;

    // Execute routing request, this does the heavy lifting
    const auto status = osrm.Route(params, result);
    ASSERT_EQ(status, osrm::Status::Ok)
                                << "Code: " << result.values["code"].get<osrm::json::String>().value
                                << std::endl
                                << "Message: " << result.values["message"].get<osrm::json::String>().value;

    auto &routes = result.values["routes"].get<osrm::json::Array>();

    // Let's just use the first route
    auto &route = routes.values.at(0).get<osrm::json::Object>();
    const auto distance = route.values["distance"].get<osrm::json::Number>().value;
    const auto duration = route.values["duration"].get<osrm::json::Number>().value;

    EXPECT_GT(distance, 0.0);
    EXPECT_GT(duration, 0.0);
}

TEST(TestOSRM, CanCalculateDistanceMatrix) {
    // given
    std::vector<std::pair<osrm::util::FloatLongitude, osrm::util::FloatLatitude> > coordinates{
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}},
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}},
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}},
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}},
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}},
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}},
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}},
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}},
            {osrm::util::FloatLongitude{-4.0}, osrm::util::FloatLatitude{55.0}}
    };

    // when
    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig("../data/scotland-latest.osrm");
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    const osrm::OSRM engine{config};

    const auto get_distance = [&engine](std::pair<osrm::util::FloatLongitude, osrm::util::FloatLatitude> source,
                                        std::pair<osrm::util::FloatLongitude, osrm::util::FloatLatitude> destination) -> double {
        osrm::RouteParameters params;
        params.coordinates.emplace_back(source.first, source.second);
        params.coordinates.emplace_back(destination.first, destination.second);

        osrm::json::Object result;
        engine.Route(params, result);
        auto &routes = result.values["routes"].get<osrm::json::Array>();
        auto &route = routes.values.at(0).get<osrm::json::Object>();
        return route.values["duration"].get<osrm::json::Number>().value;
    };

    // then
    for (const auto &source: coordinates) {
        std::vector<double> distances;
        for (const auto &target: coordinates) {
            distances.push_back(get_distance(source, target));
        }

        std::vector<std::string> text_distances;
        std::transform(std::cbegin(distances),
                       std::cend(distances),
                       std::back_inserter(text_distances),
                       [](double value) -> std::string {
                           return std::to_string(static_cast<int>(std::ceil(value)));
                       });

        LOG(INFO) << boost::format("(%1%,%2%) : %3%")
                     % source.first
                     % source.second
                     % boost::algorithm::join(text_distances, ", ");
    }
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
