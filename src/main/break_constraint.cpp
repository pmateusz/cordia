#include "break_constraint.h"

#include <boost/format.hpp>
#include <boost/date_time.hpp>

std::ostream &operator<<(std::ostream &out, const std::vector<int64> &object) {
    std::stringstream stream;

    if (object.empty()) {
        stream << "[]";
    } else {
        stream << "[";
        auto node_it = std::cbegin(object);
        stream << *node_it;
        ++node_it;

        const auto node_it_end = std::cend(object);
        for (; node_it != node_it_end; ++node_it) {
            stream << ',' << ' ' << *node_it;
        }

        stream << "]";
    }

    return out << stream.rdbuf();
}

namespace rows {
    BreakConstraint::BreakConstraint(const operations_research::RoutingDimension *dimension,
                                     int vehicle,
                                     std::vector<operations_research::IntervalVar *> break_intervals)
            : Constraint(dimension->model()->solver()),
              dimension_(dimension),
              vehicle_(vehicle),
              break_intervals_(std::move(break_intervals)),
              status_(solver()->MakeBoolVar(StrCat("status", vehicle))) {}

    void BreakConstraint::Post() {
        operations_research::RoutingModel *const model = dimension_->model();
        solver()->AddConstraint(
                solver()->MakePathConnected(model->Nexts(), {model->Start(vehicle_)},
                                            {model->End(vehicle_)}, {status_}));
        status_->WhenBound(MakeDelayedConstraintDemon0(
                solver(), this, &BreakConstraint::PathClosed, "PathClosed"));
    }

    void BreakConstraint::InitialPropagate() {
        if (status_->Bound()) {
            PathClosed();
        }
    }

    void BreakConstraint::PathClosed() {
        if (status_->Max() == 0) {
            for (operations_research::IntervalVar *const break_interval : break_intervals_) {
                break_interval->SetPerformed(false);
            }
        } else {

            operations_research::RoutingModel *const model = dimension_->model();

            std::vector<operations_research::IntervalVar *> all_intervals{std::begin(break_intervals_),
                                                                          std::end(break_intervals_)};

            std::vector<int64> nodes;
            std::vector<int64> travel_times;

            int64 current = model->Start(vehicle_);
            nodes.push_back(current);
            int64 prev = current;
            operations_research::IntervalVar *last = nullptr;

            // TODO: reconstruct path here

            while (!model->IsEnd(prev)) {
                current = model->NextVar(current)->Value();

                if (!model->IsStart(prev) || !model->IsEnd(current)) {
                    operations_research::IntervalVar *const interval = solver()->MakeIntervalVar(
                            dimension_->CumulVar(prev)->Min(), dimension_->CumulVar(prev)->Max(),
                            dimension_->FixedTransitVar(prev)->Value(), dimension_->FixedTransitVar(prev)->Value(),
                            dimension_->CumulVar(prev)->Min(), dimension_->CumulVar(current)->Max(),
                            false, StrCat(prev, "-", current));
                    all_intervals.push_back(interval);

                    if (last != nullptr) {
                        solver()->AddConstraint(solver()->MakeIntervalVarRelation(
                                interval, operations_research::Solver::STARTS_AFTER_END, last));
                    }

                    if (model->IsStart(prev)) {
                        solver()->AddConstraint(solver()->MakeEquality(interval->StartExpr(),
                                                                       dimension_->CumulVar(prev)));
                    } else {
                        solver()->AddConstraint(
                                solver()->MakeGreaterOrEqual(interval->StartExpr(),
                                                             dimension_->CumulVar(prev)));
                    }

                    if (model->IsEnd(current)) {
                        solver()->AddConstraint(solver()->MakeEquality(
                                dimension_->CumulVar(current), interval->EndExpr()));
                    } else {
                        solver()->AddConstraint(
                                solver()->MakeGreaterOrEqual(dimension_->CumulVar(current), interval->EndExpr()));
                    }

                    last = interval;
                }

                nodes.push_back(current);
                prev = current;
            }

            std::sort(std::begin(all_intervals), std::end(all_intervals),
                      [](const operations_research::IntervalVar *left,
                         operations_research::IntervalVar *right) -> bool {
                          return left->StartMin() <= right->StartMin()
                                 && left->EndMin() <= right->EndMin();
                      });

            LOG(INFO) << "Vehicle: " << vehicle_;
            LOG(INFO) << nodes;
            for (auto &interval : all_intervals) {
                LOG(INFO) << boost::format("[%1%,%2%] [%3%,%4%]")
                             % boost::posix_time::seconds(interval->StartMin())
                             % boost::posix_time::seconds(interval->StartMax())
                             % boost::posix_time::seconds(interval->EndMin())
                             % boost::posix_time::seconds(interval->EndMax());
            }

            solver()->AddConstraint(solver()->MakeDisjunctiveConstraint(all_intervals,
                                                                        StrCat("Vehicle breaks ", vehicle_)));
        }
    }
}
