#ifndef ROWS_SINGLE_STEP_SOLVER_H
#define ROWS_SINGLE_STEP_SOLVER_H

#include <unordered_map>
#include <string>

#include <ortools/base/logging.h>
#include <ortools/constraint_solver/routing.h>

#include <osrm/engine/engine_config.hpp>

#include "service_user.h"
#include "solver_wrapper.h"

namespace rows {

    class SingleStepSolver : public SolverWrapper {
    public:
        static const int64 CARE_CONTINUITY_MAX;
        static const std::string CARE_CONTINUITY_DIMENSION;

        operations_research::IntVar const *CareContinuityVar(const rows::ExtendedServiceUser &service_user) const;

        SingleStepSolver(const rows::Problem &problem,
                         osrm::EngineConfig &config,
                         const operations_research::RoutingSearchParameters &search_parameters);

        SingleStepSolver(const rows::Problem &problem,
                         osrm::EngineConfig &config,
                         const operations_research::RoutingSearchParameters &search_parameters,
                         boost::posix_time::time_duration break_time_window,
                         bool begin_end_work_day_adjustment_enabled);

        void ConfigureModel(operations_research::RoutingModel &model,
                            const std::shared_ptr<Printer> &printer,
                            const std::atomic<bool> &cancel_token) override;

        std::string GetDescription(const operations_research::RoutingModel &model,
                                   const operations_research::Assignment &solution) override;

    private:
        class CareContinuityMetrics {
        public:
            CareContinuityMetrics(const SingleStepSolver &solver, const rows::Carer &carer);

            int64 operator()(operations_research::RoutingModel::NodeIndex from,
                             operations_research::RoutingModel::NodeIndex to) const;

        private:
            std::unordered_map<operations_research::RoutingModel::NodeIndex, int64> values_;
        };

        bool care_continuity_enabled_;

        std::unordered_map<rows::ExtendedServiceUser, operations_research::IntVar *> care_continuity_;

        std::vector<CareContinuityMetrics> care_continuity_metrics_;
    };
}


#endif //ROWS_SINGLE_STEP_SOLVER_H
