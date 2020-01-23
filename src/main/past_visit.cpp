#include "past_visit.h"

#include <glog/logging.h>
#include <boost/date_time.hpp>
#include <utility>

#include "util/json.h"

rows::PastVisit::PastVisit()
        : PastVisit(0,
                    0,
                    {},
                    boost::posix_time::not_a_date_time,
                    boost::posix_time::not_a_date_time,
                    boost::posix_time::seconds(0),
                    boost::posix_time::not_a_date_time,
                    boost::posix_time::not_a_date_time,
                    boost::posix_time::seconds(0)) {}

rows::PastVisit::PastVisit(long visit,
                           long service_user,
                           std::vector<int> tasks,
                           boost::posix_time::ptime planned_check_in,
                           boost::posix_time::ptime planned_check_out,
                           boost::posix_time::time_duration planned_duration,
                           boost::posix_time::ptime real_check_in,
                           boost::posix_time::ptime real_check_out,
                           boost::posix_time::time_duration real_duration)
        : visit_{visit},
          service_user_{service_user},
          tasks_{std::move(tasks)},
          planned_check_in_{planned_check_in},
          planned_check_out_{planned_check_out},
          planned_duration_{std::move(planned_duration)},
          real_check_in_{real_check_in},
          real_check_out_{real_check_out},
          real_duration_{std::move(real_duration)} {}


void rows::to_json(nlohmann::json &json, const rows::PastVisit &visit) {
    LOG(FATAL) << "Not implemented";
}

void rows::from_json(const nlohmann::json &json, rows::PastVisit &visit) {
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

    boost::posix_time::ptime planned_check_in = boost::posix_time::min_date_time;
    const auto planned_check_in_it = json.find("planned_check_in");
    if (planned_check_in_it != std::end(json)) {
        planned_check_in = planned_check_in_it->get<boost::posix_time::ptime>();
    }

    boost::posix_time::ptime planned_check_out = boost::posix_time::min_date_time;
    const auto planned_check_out_it = json.find("planned_check_out");
    if (planned_check_out_it != std::end(json)) {
        planned_check_out = planned_check_out_it->get<boost::posix_time::ptime>();
    }

    boost::posix_time::time_duration planned_duration = boost::posix_time::seconds(0);
    const auto planned_duration_it = json.find("planned_duration");
    if (planned_duration_it != std::end(json)) {
        planned_duration = planned_duration_it->get<boost::posix_time::time_duration>();
    }

    boost::posix_time::ptime real_check_in = boost::posix_time::min_date_time;
    const auto real_check_in_it = json.find("real_check_in");
    if (real_check_in_it != std::end(json)) {
        real_check_in = real_check_in_it->get<boost::posix_time::ptime>();
    }

    boost::posix_time::ptime real_check_out = boost::posix_time::min_date_time;
    const auto real_check_out_it = json.find("real_check_out");
    if (planned_check_out_it != std::end(json)) {
        real_check_out = real_check_out_it->get<boost::posix_time::ptime>();
    }

    boost::posix_time::time_duration real_duration = boost::posix_time::seconds(0);
    const auto real_duration_it = json.find("real_duration");
    if (real_duration_it != std::end(json)) {
        real_duration = real_duration_it->get<boost::posix_time::time_duration>();
    }

    PastVisit parsed_visit{visit_id,
                           service_user_id,
                           tasks,
                           planned_check_in,
                           planned_check_out,
                           planned_duration,
                           real_check_in,
                           real_check_out,
                           real_duration};
    visit = parsed_visit;
}
