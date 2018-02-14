#include "break_constraint.h"

#include <boost/algorithm/string/join.hpp>

namespace rows {

    BreakConstraint::BreakConstraint(const operations_research::RoutingDimension *dimension,
                                     int vehicle,
                                     std::vector<operations_research::IntervalVar *> break_intervals,
                                     SolverWrapper &solver_wrapper)
            : Constraint(dimension->model()->solver()),
              dimension_(dimension),
              vehicle_(vehicle),
              break_intervals_(std::move(break_intervals)),
              status_(solver()->MakeBoolVar((boost::format("status %1%") % vehicle).str())),
              solver_(solver_wrapper) {}

    void BreakConstraint::Post() {
        operations_research::RoutingModel *const model = dimension_->model();
        operations_research::Constraint *const path_connected_const
                = solver()->MakePathConnected(model->Nexts(),
                                              {model->Start(vehicle_)},
                                              {model->End(vehicle_)},
                                              {status_});

        solver()->AddConstraint(path_connected_const);
        operations_research::Demon *const demon
                = MakeConstraintDemon0(solver(),
                                       this,
                                       &BreakConstraint::OnPathClosed,
                                       (boost::format("Path Closed %1%") % std::to_string(vehicle_)).str());
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
        } else {
            operations_research::RoutingModel *const model = dimension_->model();
            std::vector<operations_research::IntervalVar *> all_intervals;
            operations_research::IntervalVar *last_interval = nullptr;

            int64 current_index = model->NextVar(model->Start(vehicle_))->Value();
            while (!model->IsEnd(current_index)) {
                const auto next_index = model->NextVar(current_index)->Value();

                operations_research::IntervalVar *const current_interval = solver()->MakeFixedDurationIntervalVar(
                        dimension_->CumulVar(current_index),
                        dimension_->GetTransitValue(current_index, next_index, vehicle_),
                        (boost::format("%1%-%2%") % current_index % next_index).str());
                all_intervals.push_back(current_interval);

                if (last_interval != nullptr) {
                    solver()->AddConstraint(solver()->MakeIntervalVarRelation(
                            current_interval, operations_research::Solver::STARTS_AFTER_END, last_interval));
                }

                last_interval = current_interval;
                current_index = next_index;
            }

            std::copy(std::begin(break_intervals_), std::end(break_intervals_), std::back_inserter(all_intervals));
            solver()->AddConstraint(solver()->MakeStrictDisjunctiveConstraint(all_intervals,
                                                                              (boost::format("Vehicle breaks %1%")
                                                                               % vehicle_).str()));
        }
    }
}
