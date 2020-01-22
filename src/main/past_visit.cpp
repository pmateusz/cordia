#include "past_visit.h"

#include <glog/logging.h>

void rows::to_json(nlohmann::json &json, const rows::PastVisit &solution) {

}

void rows::from_json(const nlohmann::json &json, rows::PastVisit &solution) {
//    planned_check_in=representative.planned_check_in,
//    planned_check_out=representative.planned_check_out,
//    planned_duration=planned_duration_value,
//    real_check_in=representative.real_check_in,
//    real_check_out=representative.real_check_out,
//    real_duration=real_duration_value,
    long visit_id = 0;

    const auto visit_id_it = json.find("visit");
    if (visit_id_it != std::end(json)) {
        visit_id = visit_id_it->get<long>();
    }

    long service_user_id = 0;
    const auto service_user_id_it = json.find("service_user");
    if (service_user_id_it != std::end(json)) {
        service_user_id = service_user_id_it->get<long>();
    }

    std::vector<int> tasks;
    const auto tasks_it = json.find("tasks");
    if (tasks_it != std::end(json)) {
        tasks = tasks_it->get<std::vector<int>>();
    }

    int carer_count = 1;
    const auto carer_count_it = json.find("carer_count");
    if (carer_count_it != std::end(json)) {
        carer_count = carer_count_it->get<long>();
    }

    LOG(INFO) << "HERE";
}