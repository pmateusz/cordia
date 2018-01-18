#include <glog/logging.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "util/logging.h"
#include "location.h"

TEST(TestLocation, CanDeserializeFromJson) {
    // given
    const auto location_json = nlohmann::json::parse("{ \"latitude\": \"55.862\", \"longitude\": \"-4.24539\" }");
    const rows::Location expected_location("55.862", "-4.24539");
    const rows::Location::JsonLoader loader{};

    // when
    const auto actual_location = loader.Load(location_json);

    // then
    EXPECT_EQ(expected_location, actual_location);
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}