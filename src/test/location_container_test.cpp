#include <string>
#include <iostream>

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

rows::Problem Reduce(const rows::Problem &problem, const boost::filesystem::path &problem_file) {
    std::set<boost::gregorian::date> days;
    for (const auto &visit : problem.visits()) {
        days.insert(visit.date());
    }
    boost::gregorian::date day_to_use = *std::min_element(std::begin(days), std::end(days));

    if (days.size() > 1) {
        LOG(WARNING) << boost::format("Problem '%1%' contains records from several days. " \
                                              "The computed solution will be reduced to a single day: '%2%'")
                        % problem_file
                        % day_to_use;
    }

    std::vector<rows::Visit> visits_to_use;
    for (const auto &visit : problem.visits()) {
        if (visit.date() == day_to_use) {
            visits_to_use.push_back(visit);
        }
    }

    std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > carers_to_use;
    for (const auto &carer_diaries : problem.carers()) {
        for (const auto &diary : carer_diaries.second) {
            if (diary.date() == day_to_use) {
                carers_to_use.emplace_back(carer_diaries.first, std::vector<rows::Diary>{diary});
            }
        }
    }

// TODO:
//    std::vector<rows::Visit> reduced_visits;
//    std::copy(std::begin(visits_to_use), std::begin(visits_to_use) + 150, std::back_inserter(reduced_visits));

    return {visits_to_use, carers_to_use};
}

// TODO: serialize to binary format
TEST(TestLocationContainer, DISABLED_CanCalculateTravelTimes) {
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

    rows::Problem reduced_problem = Reduce(problem, problem_file);
    ASSERT_TRUE(reduced_problem.IsAdmissible());

    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig("../data/scotland-latest.osrm");
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;
    ASSERT_TRUE(config.IsValid());

    const auto locations = rows::SolverWrapper::GetUniqueLocations(problem);

    std::unordered_map<rows::Location, std::size_t> location_index;
    std::size_t index = 0;
    const auto location_end_it = std::cend(locations);
    for (auto location_it = std::cbegin(locations); location_it != location_end_it; ++location_it, ++index) {
        location_index.insert(std::make_pair(*location_it, index));
    }
    std::vector<std::vector<int64> > distance_matrix(locations.size(), std::vector<int64>(locations.size(), -1));

    rows::LocationContainer location_container{config};

    for (const auto &source_pair : location_index) {
        const auto &source_location = source_pair.first;
        const auto source_index = source_pair.second;

        LOG(INFO) << source_location;
        for (const auto &destination_pair : location_index) {
            const auto &dest_location = destination_pair.first;
            const auto dest_index = destination_pair.second;
            distance_matrix[source_index][dest_index] = (source_index != dest_index) ?
                                                        location_container.Distance(source_location, dest_location) : 0;
        }
    }
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}