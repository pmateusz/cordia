#ifndef ROWS_TWO_STEP_WORKER_H
#define ROWS_TWO_STEP_WORKER_H

#include <memory>
#include <vector>
#include <utility>

#include <ortools/constraint_solver/routing.h>

#include <glog/logging.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/date_time.hpp>
#include <util/routing.h>

#include "scheduling_worker.h"
#include "printer.h"
#include "carer.h"
#include "diary.h"
#include "second_step_solver.h"
#include "single_step_solver.h"
#include "multi_carer_solver.h"
#include "gexf_writer.h"

namespace rows {

    enum class FirstStageStrategy {
        DEFAULT,
        NONE,
        TEAMS,
        SOFT_TIME_WINDOWS
    };

    boost::optional<FirstStageStrategy> ParseFirstStageStrategy(const std::string &value);

    FirstStageStrategy GetAlias(FirstStageStrategy strategy);

    std::ostream &operator<<(std::ostream &out, FirstStageStrategy value);

    enum class ThirdStageStrategy {
        DEFAULT,
        NONE,
        DISTANCE,
        VEHICLE_REDUCTION
    };

    boost::optional<ThirdStageStrategy> ParseThirdStageStrategy(const std::string &value);

    ThirdStageStrategy GetAlias(ThirdStageStrategy strategy);

    std::ostream &operator<<(std::ostream &out, ThirdStageStrategy value);

    class ThreeStepSchedulingWorker : public SchedulingWorker {
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

        explicit ThreeStepSchedulingWorker(std::shared_ptr<rows::Printer>
                                           printer);

        ThreeStepSchedulingWorker(std::shared_ptr<rows::Printer>
                                  printer,
                                  FirstStageStrategy first_stage_strategy,
                                  ThirdStageStrategy
                                  third_stage_strategy);

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

        std::unique_ptr<rows::SolverWrapper> CreateThirdStageSolver(const operations_research::RoutingSearchParameters &search_params,
                                                                    int64 last_dropped_visit_penalty,
                                                                    std::size_t max_dropped_visits_count,
                                                                    const std::vector<rows::RouteValidatorBase::Metrics> &vehicle_metrics);

        std::vector<std::vector<int64>> SolveFirstStage(const rows::SolverWrapper &second_step_wrapper,
                                                        const operations_research::RoutingIndexManager &second_step_index_manager);

        std::vector<std::vector<int64>> SolveSecondStage(const std::vector<std::vector<int64> > &second_stage_initial_routes,
                                                         rows::SecondStepSolver &second_stage_solver,
                                                         const operations_research::RoutingIndexManager &second_stage_index_manager,
                                                         const operations_research::RoutingSearchParameters &search_params);

        void SolveThirdStage(const std::vector<std::vector<int64> > &second_stage_routes,
                             rows::SecondStepSolver &second_stage_solver);

        operations_research::RoutingSearchParameters CreateThirdStageRoutingSearchParameters();

        std::vector<CarerTeam> GetCarerTeams(const rows::Problem &problem);

        std::vector<rows::RouteValidatorBase::Metrics> GetVehicleMetrics(const std::vector<std::vector<int64> > &routes,
                                                                         const rows::SolverWrapper &second_stage_wrapper);

        boost::posix_time::time_duration visit_time_window_;
        boost::posix_time::time_duration break_time_window_;
        boost::posix_time::time_duration begin_end_shift_time_extension_;
        boost::posix_time::time_duration pre_opt_time_limit_;
        boost::posix_time::time_duration opt_time_limit_;
        boost::posix_time::time_duration post_opt_time_limit_;

        std::string output_file_;

        std::shared_ptr<Printer> printer_;
        FirstStageStrategy first_stage_strategy_;
        ThirdStageStrategy third_stage_strategy_;

        osrm::EngineConfig routing_parameters_;
        Problem problem_;

        SolutionValidator solution_validator_;
        GexfWriter solution_writer_;
    };
};


#endif //ROWS_TWO_STEP_WORKER_H
