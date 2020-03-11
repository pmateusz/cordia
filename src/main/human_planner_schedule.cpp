#include "human_planner_schedule.h"

const std::vector<std::string> rows::HumanPlannerSchedule::NO_CARERS{};

rows::HumanPlannerSchedule::HumanPlannerSchedule(const std::vector<ScheduledVisit> &scheduled_visits) {
    std::unordered_set<boost::gregorian::date> dates;
    for (const auto &visit : scheduled_visits) {
        dates.insert(visit.datetime().date());

        if (visit.calendar_visit() && visit.carer()) {
            scheduled_visits_[visit.calendar_visit()->id()].push_back(visit.carer()->sap_number());
        }
    }

    if (!dates.empty()) {
        CHECK_EQ(dates.size(), 1);
        date_ = *dates.begin();
    }
}

const std::vector<std::string> &rows::HumanPlannerSchedule::find_visit_by_id(std::size_t visit_id) const {
    const auto visit_it = scheduled_visits_.find(visit_id);
    if (visit_it != std::cend(scheduled_visits_)) {
        return visit_it->second;
    }
    return NO_CARERS;
}

void rows::from_json(const nlohmann::json &json, rows::HumanPlannerSchedule &solution) {
    auto scheduled_visits = json.at("visits").get<std::vector<rows::ScheduledVisit >>();
    HumanPlannerSchedule output_solution(std::move(scheduled_visits));
    solution = std::move(output_solution);
}