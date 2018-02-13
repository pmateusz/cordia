#include "break_constraint.h"

#include <ostream>
#include <vector>
#include <sstream>

#include <ortools/base/integral_types.h>
#include <ortools/base/join.h>

#include <glog/logging.h>

#include <boost/format.hpp>
#include <boost/date_time.hpp>
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
            // TODO: put breaks on the road - you are allowed to move values

            LOG(ERROR) << "Before vehicle: " << vehicle_;
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

            operations_research::RoutingModel *const model = dimension_->model();

            std::vector<operations_research::IntervalVar *> all_intervals;

            std::vector<int64> nodes;
            std::vector<int64> travel_times;

            const auto carer = solver_.Carer(vehicle_);
//
//

            int64 current = model->Start(vehicle_);
            std::vector<rows::ScheduledVisit> visits;
            nodes.push_back(current);

            while (!model->IsEnd(current)) {
                current = model->NextVar(current)->Value();
                nodes.push_back(current);

//                int64 prev = current;
//                current = model->NextVar(current)->Value();
//                nodes.push_back(current);
//
//                if (!model->IsEnd(current)) {
//                    visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN,
//                                        carer,
//                                        solver_.NodeToVisit(model->IndexToNode(current)));
//                }
//
//                if (!model->IsStart(prev)) {
//                    operations_research::IntervalVar *const interval = solver()->MakeFixedDurationIntervalVar(
//                            dimension_->CumulVar(prev),
//                            dimension_->GetTransitValue(prev, current, vehicle_),
//                            (boost::format("%1%-%2%") % prev % current).str());
//                    all_intervals.push_back(interval);
//
//                    if (last != nullptr) {
//                        solver()->AddConstraint(solver()->MakeIntervalVarRelation(
//                                interval, operations_research::Solver::STARTS_AFTER_END, last));
//                    }
////
////                    solver()->AddConstraint(
////                            solver()->MakeGreaterOrEqual(interval->StartExpr(), dimension_->CumulVar(prev)));
////                    solver()->AddConstraint(
////                            solver()->MakeLessOrEqual(interval->EndExpr(), dimension_->CumulVar(current)));
//
//                    last = interval;
//                }
            }
            const auto visit_count = nodes.size() - 1;
            for (auto node_pos = 1; node_pos < visit_count; ++node_pos) {
                const auto node = nodes[node_pos];
                visits.emplace_back(ScheduledVisit::VisitType::UNKNOWN,
                                    carer,
                                    solver_.NodeToVisit(model->IndexToNode(node)));
            }

            LOG(ERROR) << "Visits: ";
            for (const auto &visit : visits) {
                LOG(ERROR) << boost::format("[%1%,%2%] %3%")
                              % boost::posix_time::seconds(solver_.GetBeginWindow(visit.datetime().time_of_day()))
                              % boost::posix_time::seconds(solver_.GetEndWindow(visit.datetime().time_of_day()))
                              % visit.duration();
            }

            for (auto node_pos = 0; node_pos < nodes.size(); ++node_pos) {
                const auto node = nodes[node_pos];
                if (model->IsEnd(node)) {
                    LOG(INFO) << boost::format("%|5| [%s,%s]")
                                 % node
                                 % seconds(dimension_->CumulVar(node)->Min())
                                 % seconds(dimension_->CumulVar(node)->Max());
                } else {
                    LOG(INFO) << boost::format("%|5| [%s,%s] %s")
                                 % node
                                 % seconds(dimension_->CumulVar(node)->Min())
                                 % seconds(dimension_->CumulVar(node)->Max())
                                 % seconds(dimension_->GetTransitValue(nodes[node_pos], nodes[node_pos + 1], vehicle_));
                }
            }

            operations_research::IntervalVar *last = nullptr;
            for (auto node_pos = 1; node_pos < nodes.size() - 1; ++node_pos) {
                const auto from_node = nodes[node_pos];
                const auto to_node = nodes[node_pos];

                operations_research::IntervalVar *const interval = solver()->MakeFixedDurationIntervalVar(
                        dimension_->CumulVar(from_node),
                        dimension_->GetTransitValue(from_node, to_node, vehicle_),
                        (boost::format("%1%-%2%") % from_node % to_node).str());
                all_intervals.push_back(interval);

                if (last != nullptr) {
                    solver()->AddConstraint(solver()->MakeIntervalVarRelation(
                            interval, operations_research::Solver::STARTS_AFTER_END, last));
                }

                last = interval;
            }

            std::vector<std::string> text_nodes;
            for (const auto &node : nodes) {
                text_nodes.push_back(std::to_string(node));
            }

            LOG(ERROR) << boost::join(text_nodes, ", ");

            Route route{carer, visits};
            SimpleRouteValidator validator;
            const auto validation_result = validator.Validate(route, solver_);
            DCHECK(!validation_result.error());

            for (const auto &interval : break_intervals_) {
                all_intervals.push_back(interval);
            }

            std::sort(std::begin(all_intervals), std::end(all_intervals),
                      [](const operations_research::IntervalVar *left,
                         operations_research::IntervalVar *right) -> bool {
                          return left->EndMax() <= right->EndMax();
                      });

            LOG(ERROR) << "After vehicle: " << vehicle_
                       << " " << seconds(dimension_->CumulVar(dimension_->model()->Start(vehicle_))->Min())
                       << " " << seconds(dimension_->CumulVar(dimension_->model()->Start(vehicle_))->Max());
            for (auto &interval : all_intervals) {
                LOG(ERROR) << boost::format("[%1%,%2%] [%3%,%4%] [%5%,%6%]")
                              % boost::posix_time::seconds(interval->StartMin())
                              % boost::posix_time::seconds(interval->StartMax())
                              % boost::posix_time::seconds(interval->DurationMin())
                              % boost::posix_time::seconds(interval->DurationMax())
                              % boost::posix_time::seconds(interval->EndMin())
                              % boost::posix_time::seconds(interval->EndMax());
            }

//            const auto &failure_interceptor = []() -> void {
//                LOG(INFO) << "HERE";
//            };
//
//            solver()->set_fail_intercept(failure_interceptor);

            solver()->AddConstraint(solver()->MakeStrictDisjunctiveConstraint(all_intervals,
                                                                              (boost::format("Vehicle breaks %1%")
                                                                               % vehicle_).str()));

//            solver()->clear_fail_intercept();
        }
    }
}
