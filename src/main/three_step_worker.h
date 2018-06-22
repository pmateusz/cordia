#ifndef ROWS_TWO_STEP_WORKER_H
#define ROWS_TWO_STEP_WORKER_H

#include <memory>
#include <vector>
#include <utility>

#include <ortools/constraint_solver/routing.h>

#include <glog/logging.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/date_time.hpp>

#include "scheduling_worker.h"
#include "printer.h"
#include "carer.h"
#include "diary.h"
#include "second_step_solver.h"
#include "single_step_solver.h"

namespace rows {

    class ThreeStepSchedulingWorker : public rows::SchedulingWorker {
    public:
        enum class Formula {
            DEFAULT, DISTANCE, VEHICLE_REDUCTION
        };

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

        explicit ThreeStepSchedulingWorker(std::shared_ptr<rows::Printer> printer);

        ThreeStepSchedulingWorker(std::shared_ptr<rows::Printer> printer, Formula formula);

        void Run() override;

        bool Init(rows::Problem problem,
                  osrm::EngineConfig routing_config,
                  std::string output_file,
                  boost::posix_time::time_duration visit_time_window,
                  boost::posix_time::time_duration break_time_window,
                  boost::posix_time::time_duration begin_end_shift_time_extension,
                  boost::posix_time::time_duration pre_opt_time_limit,
                  boost::posix_time::time_duration opt_time_limit,
                  boost::posix_time::time_duration post_opt_time_limit);

    private:
        std::unique_ptr<rows::SolverWrapper>
        CreateThirdStageSolver(const operations_research::RoutingSearchParameters &search_params,
                               int64 last_dropped_visit_penalty,
                               std::size_t max_dropped_visits_count,
                               const std::vector<rows::RouteValidatorBase::Metrics> & vehicle_metrics);

        std::vector<CarerTeam> GetCarerTeams(const rows::Problem &problem);

        std::string output_file_;

        std::shared_ptr<rows::Printer> printer_;
        Formula formula_;
        bool lock_partial_paths_;

        osrm::EngineConfig routing_parameters_;
        rows::Problem problem_;

        boost::posix_time::time_duration visit_time_window_;
        boost::posix_time::time_duration break_time_window_;
        boost::posix_time::time_duration begin_end_shift_time_extension_;
        boost::posix_time::time_duration pre_opt_time_limit_;
        boost::posix_time::time_duration opt_time_limit_;
        boost::posix_time::time_duration post_opt_time_limit_;
    };
}


#endif //ROWS_TWO_STEP_WORKER_H
