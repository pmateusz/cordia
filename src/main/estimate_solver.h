#ifndef ROWS_ESTIMATE_SOLVER_H
#define ROWS_ESTIMATE_SOLVER_H


#include "solver_wrapper.h"
#include "human_planner_schedule.h"

namespace rows {

    class EstimateSolver : public SolverWrapper {
    public:
        EstimateSolver(const rows::ProblemData &problem_data,
                       const rows::HumanPlannerSchedule &human_planner_schedule,
                       const operations_research::RoutingSearchParameters &search_parameters,
                       boost::posix_time::time_duration visit_time_window,
                       boost::posix_time::time_duration break_time_window,
                       boost::posix_time::time_duration begin_end_work_day_adjustment,
                       boost::posix_time::time_duration no_progress_time_limit);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            std::shared_ptr<const std::atomic<bool> > cancel_token,
                            double normalization_factor) override;

    private:
        const rows::HumanPlannerSchedule &human_planner_schedule_;

        boost::posix_time::time_duration no_progress_time_limit_;
    };
}


#endif //ROWS_ESTIMATE_SOLVER_H
