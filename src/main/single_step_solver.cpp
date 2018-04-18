
#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/median.hpp>
#include <boost/accumulators/statistics/variance.hpp>

#include "single_step_solver.h"

namespace rows {

    const int64 SingleStepSolver::CARE_CONTINUITY_MAX = 10000;

    const std::string SingleStepSolver::CARE_CONTINUITY_DIMENSION{"CareContinuity"};

    SingleStepSolver::SingleStepSolver(const rows::Problem &problem, osrm::EngineConfig &config,
                                       const operations_research::RoutingSearchParameters &search_parameters)
            : SolverWrapper(problem, config, search_parameters),
              care_continuity_enabled_(false),
              care_continuity_(),
              care_continuity_metrics_() {

        for (const auto &service_user : problem_.service_users()) {
            care_continuity_.insert(std::make_pair(service_user, nullptr));
        }
    }

    operations_research::IntVar const *SingleStepSolver::CareContinuityVar(
            const rows::ExtendedServiceUser &service_user) const {
        const auto service_user_it = care_continuity_.find(service_user);
        DCHECK(service_user_it != std::cend(care_continuity_));
        return service_user_it->second;
    }

    void SingleStepSolver::ConfigureModel(operations_research::RoutingModel &model,
                                          const std::shared_ptr<Printer> &printer,
                                          const std::atomic<bool> &cancel_token) {
        static const auto START_FROM_ZERO_SERVICE_SATISFACTION = true;

        SolverWrapper::ConfigureModel(model, printer, cancel_token);

        if (care_continuity_enabled_) {
            for (const auto &carer_pair :problem_.carers()) {
                care_continuity_metrics_.emplace_back(*this, carer_pair.first);
            }

            std::vector<operations_research::RoutingModel::NodeEvaluator2 *> care_continuity_evaluators;
            for (const auto &carer_metrics :care_continuity_metrics_) {
                care_continuity_evaluators.
                        push_back(
                        NewPermanentCallback(&carer_metrics, &CareContinuityMetrics::operator()));
            }

            model.AddDimensionWithVehicleTransits(care_continuity_evaluators,
                                                  0,
                                                  CARE_CONTINUITY_MAX,
                                                  START_FROM_ZERO_SERVICE_SATISFACTION,
                                                  CARE_CONTINUITY_DIMENSION);

            operations_research::RoutingDimension const *care_continuity_dimension = model.GetMutableDimension(
                    rows::SingleStepSolver::CARE_CONTINUITY_DIMENSION);

            // define and maximize the service satisfaction
            for (const auto &service_user :problem_.service_users()) {
                std::vector<operations_research::IntVar *> service_user_visits;

                for (const auto &visit :problem_.visits()) {
                    if (visit.service_user() != service_user) {
                        continue;
                    }

                    const auto visit_it = visit_index_.find(visit);
                    DCHECK(visit_it != std::end(visit_index_));
                    for (const auto &visit_node : visit_it->second) {
                        service_user_visits.push_back(
                                care_continuity_dimension->TransitVar(model.NodeToIndex(visit_node)));
                    }
                }

                auto find_it = care_continuity_.find(service_user);
                DCHECK(find_it != std::end(care_continuity_));

                const auto care_satisfaction = model.solver()->MakeSum(service_user_visits)->Var();
                find_it->second = care_satisfaction;

                model.AddToAssignment(care_satisfaction);
                model.AddVariableMaximizedByFinalizer(care_satisfaction);
            }
        }
    }

    std::string SingleStepSolver::GetDescription(const operations_research::RoutingModel &model,
                                                 const operations_research::Assignment &solution) {
        auto description = SolverWrapper::GetDescription(model, solution);

        if (care_continuity_enabled_) {
            boost::accumulators::accumulator_set<double,
                    boost::accumulators::stats<
                            boost::accumulators::tag::mean,
                            boost::accumulators::tag::median,
                            boost::accumulators::tag::variance> > care_continuity_stats;

            for (const auto &user_satisfaction_pair : care_continuity_) {
                care_continuity_stats(solution.Value(user_satisfaction_pair.second));
            }

            description.append((boost::format("Care continuity: mean: %1% median: %2% stddev: %3%\n")
                                % boost::accumulators::mean(care_continuity_stats)
                                % boost::accumulators::median(care_continuity_stats)
                                % std::sqrt(boost::accumulators::variance(care_continuity_stats))).str()
            );
        }

        return description;
    }


    SingleStepSolver::CareContinuityMetrics::CareContinuityMetrics(const SingleStepSolver &solver,
                                                                   const rows::Carer &carer)
            : values_() {
        const auto visit_it_end = std::cend(solver.visit_index_);
        for (auto visit_it = std::cbegin(solver.visit_index_); visit_it != visit_it_end; ++visit_it) {
            const auto &service_user = solver.User(visit_it->first.service_user());
            if (service_user.IsPreferred(carer)) {
                for (const auto visit_node : visit_it->second) {
                    values_.insert(std::make_pair(visit_node, service_user.Preference(carer)));
                }
            }
        }
    }

    int64 SingleStepSolver::CareContinuityMetrics::operator()(operations_research::RoutingModel::NodeIndex from,
                                                              operations_research::RoutingModel::NodeIndex to) const {
        const auto to_it = values_.find(to);
        if (to_it == std::end(values_)) {
            return 0;
        }
        return to_it->second;
    }
}