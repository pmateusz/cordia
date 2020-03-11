#ifndef ROWS_HUMAN_PLANNER_SCHEDULE_H
#define ROWS_HUMAN_PLANNER_SCHEDULE_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <boost/date_time.hpp>

#include "scheduled_visit.h"

namespace rows {

    class HumanPlannerSchedule {
    public:
        HumanPlannerSchedule()
                : HumanPlannerSchedule(std::vector<ScheduledVisit>{}) {}

        explicit HumanPlannerSchedule(const std::vector<ScheduledVisit> &scheduled_visits);

        const boost::gregorian::date &date() const { return date_; }

        const std::vector<std::string> &find_visit_by_id(std::size_t visit_id) const;

    private:
        static const std::vector<std::string> NO_CARERS;

        boost::gregorian::date date_;
        std::unordered_map<std::size_t, std::vector<std::string>> scheduled_visits_;
    };

    void from_json(const nlohmann::json &json, HumanPlannerSchedule &solution);
}

#endif //ROWS_HUMAN_PLANNER_SCHEDULE_H
