#ifndef ROWS_TWO_STEP_WORKER_H
#define ROWS_TWO_STEP_WORKER_H

#include <memory>
#include <vector>
#include <utility>

#include <ortools/constraint_solver/routing.h>

#include <glog/logging.h>

#include <boost/algorithm/string/join.hpp>

#include "scheduling_worker.h"
#include "printer.h"
#include "carer.h"
#include "diary.h"
#include "two_step_solver.h"
#include "single_step_solver.h"

namespace rows {

    class TwoStepSchedulingWorker : public rows::SchedulingWorker {
    public:

        class CarerTeam {
        public:
            explicit CarerTeam(std::pair<rows::Carer, rows::Diary> member);

            void Add(std::pair<rows::Carer, rows::Diary> member);

            std::size_t size() const;

            std::vector<rows::Carer> Members() const;

            const std::vector<std::pair<rows::Carer, rows::Diary> > &FullMembers() const;;

            std::vector<rows::Carer> AvailableMembers(boost::posix_time::ptime date_time,
                                                      const boost::posix_time::time_duration &adjustment) const;


            const rows::Diary &Diary() const;

        private:
            rows::Diary diary_;
            std::vector<std::pair<rows::Carer, rows::Diary> > members_;
        };

        explicit TwoStepSchedulingWorker(std::shared_ptr<rows::Printer> printer);

        void Run() override;

        bool Init(rows::Problem problem, osrm::EngineConfig routing_config, std::string output_file);

    private:
        std::vector<CarerTeam> GetCarerTeams(const rows::Problem &problem);

        std::string output_file_;

        std::shared_ptr<rows::Printer> printer_;
        bool lock_partial_paths_;

        osrm::EngineConfig routing_parameters_;
        rows::Problem problem_;
    };
}


#endif //ROWS_TWO_STEP_WORKER_H
