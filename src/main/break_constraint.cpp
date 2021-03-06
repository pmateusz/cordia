#include "break_constraint.h"

#include <boost/algorithm/string/join.hpp>
#include <boost/date_time/time.hpp>

#include "solver_wrapper.h"

namespace rows {

    BreakConstraint::BreakConstraint(const operations_research::RoutingDimension *dimension,
                                     const operations_research::RoutingIndexManager *index_manager,
                                     int vehicle,
                                     std::vector<operations_research::IntervalVar *> break_intervals,
                                     RealProblemData &problem_data)
            : Constraint(dimension->model()->solver()),
              dimension_(dimension),
              index_manager_(index_manager),
              vehicle_(vehicle),
              break_intervals_(std::move(break_intervals)),
              status_(solver()->MakeBoolVar((boost::format("status %1%") % vehicle).str())),
              problem_data_(problem_data) {}

    void BreakConstraint::Post() {
        operations_research::RoutingModel *const model = dimension_->model();
        operations_research::Constraint *const path_connected_const
                = solver()->MakePathConnected(model->Nexts(),
                                              {model->Start(vehicle_)},
                                              {model->End(vehicle_)},
                                              {status_});

        solver()->AddConstraint(path_connected_const);
        operations_research::Demon *const demon = solver()->MakeConstraintInitialPropagateCallback(this);
        status_->WhenBound(demon);
    }

    void BreakConstraint::InitialPropagate() {
        if (status_->Bound()) {
            OnPathClosed();
        }
    }

    void BreakConstraint::OnPathClosed() {
        using boost::posix_time::time_duration;
        using boost::posix_time::seconds;

        if (status_->Max() == 0) {
            for (operations_research::IntervalVar *const break_interval : break_intervals_) {
                break_interval->SetPerformed(false);
            }
            return;
        }

        const auto initial_number_of_failures = solver()->failures();

        std::vector<operations_research::IntervalVar *> all_intervals;
        operations_research::IntervalVar *last_travel_interval = nullptr;
        operations_research::IntervalVar *last_last_visit_interval = nullptr;
        operations_research::IntervalVar *last_visit_interval = nullptr;

        operations_research::RoutingModel *const model = dimension_->model();
        int64 current_index = model->Start(vehicle_);
        while (!model->IsEnd(current_index)) {
            const auto current_node = index_manager_->IndexToNode(current_index);
            const auto next_index = model->NextVar(current_index)->Value();
            const auto next_node = index_manager_->IndexToNode(next_index);

            // create visit interval
            if (RealProblemData::DEPOT != current_node) {
                const auto visit_duration = problem_data_.ServiceTime(current_node);
                DCHECK_GT(visit_duration, 0);

                operations_research::IntervalVar *const visit_interval = solver()->MakeFixedDurationIntervalVar(
                        dimension_->CumulVar(current_index),
                        visit_duration,
                        (boost::format("visit %1%") % current_index).str());
                all_intervals.push_back(visit_interval);

                if (last_travel_interval) {
                    solver()->AddConstraint(solver()->MakeIntervalVarRelation(visit_interval,
                                                                              operations_research::Solver::STARTS_AFTER_END,
                                                                              last_travel_interval));
                }

                last_last_visit_interval = last_visit_interval;
                last_visit_interval = visit_interval;
            } else {
                last_last_visit_interval = last_visit_interval;
                last_visit_interval = nullptr;
            }

            // create travel interval
            const auto travel_duration = problem_data_.Distance(current_node, next_node);
            if (travel_duration > 0) {
                int64 min_travel_start = 0;
                if (last_visit_interval != nullptr) {
                    min_travel_start = last_visit_interval->EndMin();
                }

                const auto max_travel_start = std::min(dimension_->CumulVar(next_index)->Max() - travel_duration,
                                                       RealProblemData::SECONDS_IN_DIMENSION);
                if (min_travel_start > max_travel_start) {
                    solver()->Fail();
                }

                operations_research::IntervalVar *const travel_interval = solver()->MakeFixedDurationIntervalVar(
                        min_travel_start,
                        max_travel_start,
                        travel_duration,
                        false,
                        (boost::format("travel %1%-%2%") % current_index % next_index).str());
                all_intervals.push_back(travel_interval);

                if (last_visit_interval) {
                    solver()->AddConstraint(solver()->MakeIntervalVarRelation(travel_interval,
                                                                              operations_research::Solver::STARTS_AFTER_END,
                                                                              last_visit_interval));
                    solver()->AddConstraint(
                            solver()->MakeLessOrEqual(last_visit_interval->EndExpr(), travel_interval->StartExpr()));
                    CHECK_GE(travel_interval->StartMin(), last_visit_interval->EndMin());
                }

                last_travel_interval = travel_interval;
            } else {
                if (last_last_visit_interval && last_visit_interval) {
                    solver()->AddConstraint(solver()->MakeIntervalVarRelation(last_visit_interval,
                                                                              operations_research::Solver::STARTS_AFTER_END,
                                                                              last_last_visit_interval));
                }
                last_travel_interval = nullptr;
            }

            current_index = next_index;
        }

        if (break_intervals_.size() > 1) {
            const auto break_interval_end = std::end(break_intervals_);
            auto last_break_it = std::begin(break_intervals_);
            auto break_interval_it = std::next(last_break_it);
            while (break_interval_it != break_interval_end) {
                solver()->AddConstraint(solver()->MakeIntervalVarRelation(*break_interval_it,
                                                                          operations_research::Solver::STARTS_AFTER_END,
                                                                          *last_break_it));

                last_break_it = break_interval_it;
                break_interval_it = std::next(break_interval_it);
            }
        }

        const auto current_number_of_failures = solver()->failures();
        if (current_number_of_failures > initial_number_of_failures) {
            LOG(WARNING) << "Registered a failure and have no way to jump...";
        } else {
            std::copy(std::begin(break_intervals_), std::end(break_intervals_), std::back_inserter(all_intervals));
            solver()->AddConstraint(solver()->MakeDisjunctiveConstraint(all_intervals,
                                                                        (boost::format("Vehicle breaks %1%")
                                                                         % vehicle_).str()));
        }
    }
}
