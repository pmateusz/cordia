#include "break_constraint.h"

#include <ostream>
#include <vector>
#include <sstream>

#include <ortools/base/integral_types.h>

#include <glog/logging.h>

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

    out << stream.rdbuf();

    return out;
}

namespace rows {

    BreakConstraint::BreakConstraint(const operations_research::RoutingDimension *dimension,
                                     int vehicle,
                                     std::vector<operations_research::IntervalVar *> break_intervals,
                                     SolverWrapper &solver_wrapper)
            : Constraint(dimension->model()->solver()),
              dimension_(dimension),
              vehicle_(vehicle),
              break_intervals_(std::move(break_intervals)),
              status_(solver()->MakeBoolVar(StrCat("status", vehicle))),
              solver_(solver_wrapper) {}

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
            LOG(ERROR) << "Before vehicle: " << vehicle_;

            operations_research::RoutingModel *const model = dimension_->model();

            std::vector<operations_research::IntervalVar *> all_intervals{std::begin(break_intervals_),
                                                                          std::end(break_intervals_)};

            std::vector<int64> nodes;
            std::vector<int64> travel_times;

            int64 current = model->Start(vehicle_);
            nodes.push_back(current);
            int64 prev = current;
            operations_research::IntervalVar *last = nullptr;

            const auto carer = solver_.Carer(vehicle_);
            std::vector<rows::ScheduledVisit> visits;

            while (!model->IsEnd(prev)) {
                current = model->NextVar(current)->Value();

                if (!model->IsStart(prev) || !model->IsEnd(current)) {
                    if (!model->IsStart(prev)) {
                        visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN,
                                            carer,
                                            solver_.NodeToVisit(model->IndexToNode(prev)));
                    }

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

            Route route{carer, visits};
            RouteValidator validator;
            const auto validation_result = validator.Validate(route, solver_);
            DCHECK(!validation_result.error());

            LOG(ERROR) << "Breaks: ";
            for (const auto &local_break : break_intervals_) {
                LOG(ERROR) << boost::format("[%1%,%2%] [%3%,%4%] [%5%,%6%]")
                              % boost::posix_time::seconds(local_break->StartMin())
                              % boost::posix_time::seconds(local_break->StartMax())
                              % boost::posix_time::seconds(local_break->DurationMin())
                              % boost::posix_time::seconds(local_break->DurationMax())
                              % boost::posix_time::seconds(local_break->EndMin())
                              % boost::posix_time::seconds(local_break->EndMax());
            }

            LOG(ERROR) << "Visits: ";
            for (const auto &visit : visits) {
                LOG(ERROR) << boost::format("[%1%,%2%] %3%")
                              % boost::posix_time::seconds(solver_.GetBeginWindow(visit.datetime().time_of_day()))
                              % boost::posix_time::seconds(solver_.GetEndWindow(visit.datetime().time_of_day()))
                              % visit.duration();
            }

            std::sort(std::begin(all_intervals), std::end(all_intervals),
                      [](const operations_research::IntervalVar *left,
                         operations_research::IntervalVar *right) -> bool {
                          return left->StartMin() <= right->StartMin()
                                 && left->EndMin() <= right->EndMin();
                      });

            LOG(ERROR) << "After vehicle: " << vehicle_;
            for (auto &interval : all_intervals) {
                LOG(ERROR) << boost::format("[%1%,%2%] [%3%,%4%] [%5%,%6%]")
                              % boost::posix_time::seconds(interval->StartMin())
                              % boost::posix_time::seconds(interval->StartMax())
                              % boost::posix_time::seconds(interval->DurationMin())
                              % boost::posix_time::seconds(interval->DurationMax())
                              % boost::posix_time::seconds(interval->EndMin())
                              % boost::posix_time::seconds(interval->EndMax());
            }

            solver()->AddConstraint(solver()->MakeDisjunctiveConstraint(all_intervals,
                                                                        StrCat("Vehicle breaks ", vehicle_)));
        }
    }
}
