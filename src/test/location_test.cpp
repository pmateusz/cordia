#include <algorithm>
#include <unordered_set>
#include <string>
#include <vector>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include <scheduled_visit.h>

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

TEST(TestScheduledVisit, CanExecuteContainerOperations) {
    // given
    const rows::ScheduledVisit visit{
            rows::ScheduledVisit::VisitType::UNKNOWN,
            rows::Carer{"107955"},
            boost::posix_time::ptime(boost::gregorian::date(2017, 2, 1), boost::posix_time::time_duration(8, 15, 0)),
            boost::posix_time::minutes(45),
            boost::posix_time::ptime(boost::gregorian::date(2017, 2, 1), boost::posix_time::time_duration(8, 24, 0)),
            boost::posix_time::ptime(boost::gregorian::date(2017, 2, 1), boost::posix_time::time_duration(8, 51, 0)),
            boost::make_optional(rows::CalendarVisit{
                    rows::ServiceUser{"9082143"},
                    rows::Address{"1", "Dusk Place", "Glasgow", "G13 4LH"},
                    boost::make_optional(rows::Location{"55.8886", "-4.34296"}),
                    boost::posix_time::ptime(boost::gregorian::date(2017, 2, 1),
                                             boost::posix_time::time_duration(8, 15, 0)),
                    boost::posix_time::minutes(45)
            })};

    // when
    std::vector<rows::ScheduledVisit> visits{visit};
    std::unordered_set<rows::ScheduledVisit> visits_set{visit};

    // then
    EXPECT_NE(std::find(std::begin(visits), std::end(visits), visit), std::end(visits));
    EXPECT_NE(visits_set.find(visit), std::end(visits_set));
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}