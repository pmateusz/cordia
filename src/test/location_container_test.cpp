#include <string>
#include <iostream>
#include <algorithm>
#include <unordered_set>

#include <osrm/match_parameters.hpp>
#include <osrm/nearest_parameters.hpp>
#include <osrm/route_parameters.hpp>
#include <osrm/table_parameters.hpp>
#include <osrm/trip_parameters.hpp>

#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>

#include <osrm/osrm.hpp>

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/range/irange.hpp>
#include <solver_wrapper.h>

#include "problem.h"

#include "util/logging.h"

TEST(TestLocationContainer, CanCalculateTravelTimes) {
    boost::filesystem::path problem_file(boost::filesystem::canonical("../problem.json"));

    std::ifstream problem_stream(problem_file.c_str());
    ASSERT_TRUE(problem_stream.is_open());

    nlohmann::json json;
    try {
        problem_stream >> json;
    } catch (...) {
        LOG(ERROR) << boost::current_exception_diagnostic_information();
        FAIL();
    }

    rows::Problem problem;
    try {
        rows::Problem::JsonLoader json_loader;
        problem = json_loader.Load(json);
    } catch (const std::domain_error &ex) {
        LOG(ERROR) << boost::format("Failed to parse file '%1%' due to error: '%2%'") % problem_file % ex.what();
        FAIL();
    }

    const auto time_span = problem.Timespan();
    rows::Problem reduced_problem = problem.Trim(time_span.first, boost::posix_time::hours(24));
    ASSERT_TRUE(reduced_problem.IsAdmissible());

    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig("../data/scotland-latest.osrm");
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;
    ASSERT_TRUE(config.IsValid());

    std::unordered_set<rows::Location> locations;
    for (const auto &visit : problem.visits()) {
        const auto &location = visit.location();
        if (location) {
            locations.insert(location.get());
        }
    }

    std::unordered_map<rows::Location, std::size_t> location_index;
    std::size_t index = 0;
    const auto location_end_it = std::end(locations);
    for (auto location_it = std::begin(locations); location_it != location_end_it; ++location_it, ++index) {
        location_index.insert(std::make_pair(*location_it, index));
    }
    std::vector<std::vector<int64> > distance_matrix(locations.size(), std::vector<int64>(locations.size(), -1));

    rows::LocationContainer location_container{config};

    for (const auto &source_pair : location_index) {
        const auto &source_location = source_pair.first;
        const auto source_index = source_pair.second;

        for (const auto &destination_pair : location_index) {
            const auto &dest_location = destination_pair.first;
            const auto dest_index = destination_pair.second;
            distance_matrix[source_index][dest_index] = (source_index != dest_index) ?
                                                        location_container.Distance(source_location, dest_location) : 0;
        }
    }

    // calculate minimum and maximum distance
    int64 min = std::numeric_limits<int64>::max(), max = std::numeric_limits<int64>::min();
    for (const auto &row : distance_matrix) {
        const auto pair = std::minmax(std::begin(row), std::end(row));
        if (*pair.first > 0) {
            min = std::min(*pair.first, min);
        }
        max = std::max(*pair.second, max);
    }

    EXPECT_GT(max, 0);
    EXPECT_GT(min, 0);
    LOG(INFO) << "Max: " << max << " Min: " << min;
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
