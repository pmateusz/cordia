#ifndef ROWS_PAST_VISIT_H
#define ROWS_PAST_VISIT_H

#include <nlohmann/json.hpp>
#include <boost/date_time.hpp>

namespace rows {

    class PastVisit {
    public:
        PastVisit();

        PastVisit(long visit,
                  long service_user,
                  std::vector<int> tasks,
                  boost::posix_time::ptime planned_check_in,
                  boost::posix_time::ptime planned_check_out,
                  boost::posix_time::time_duration planned_duration,
                  boost::posix_time::ptime real_check_in,
                  boost::posix_time::ptime real_check_out,
                  boost::posix_time::time_duration real_duration);

        inline long service_user() const { return service_user_; }

        inline boost::gregorian::date date() const { return planned_check_in_.date(); }

        inline const boost::posix_time::ptime &planned_check_in() const { return planned_check_in_; }

        inline const std::vector<int> &tasks() const { return tasks_; }

        inline const boost::posix_time::time_duration &real_duration() const { return real_duration_; }

    private:
        long visit_;
        long service_user_;
        std::vector<int> tasks_;
        boost::posix_time::ptime planned_check_in_;
        boost::posix_time::ptime planned_check_out_;
        boost::posix_time::time_duration planned_duration_;
        boost::posix_time::ptime real_check_in_;
        boost::posix_time::ptime real_check_out_;
        boost::posix_time::time_duration real_duration_;
    };

    void to_json(nlohmann::json &json, const PastVisit &solution);

    void from_json(const nlohmann::json &json, PastVisit &solution);
}


#endif //ROWS_PAST_VISIT_H
