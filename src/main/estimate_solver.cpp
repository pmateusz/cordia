#include "estimate_solver.h"
#include "progress_printer_monitor.h"
#include "min_dropped_visits_collector.h"
#include "cancel_search_limit.h"
#include "stalled_search_limit.h"

namespace rows {

    EstimateSolver::EstimateSolver(const rows::ProblemData &problem_data,
                                   const rows::HumanPlannerSchedule &human_planner_schedule,
                                   const operations_research::RoutingSearchParameters &search_parameters,
                                   boost::posix_time::time_duration visit_time_window,
                                   boost::posix_time::time_duration break_time_window,
                                   boost::posix_time::time_duration begin_end_work_day_adjustment,
                                   boost::posix_time::time_duration no_progress_time_limit)
            : SolverWrapper(problem_data,
                            search_parameters,
                            std::move(visit_time_window),
                            std::move(break_time_window),
                            std::move(begin_end_work_day_adjustment)),
              human_planner_schedule_{human_planner_schedule},
              no_progress_time_limit_{std::move(no_progress_time_limit)} {}

    void EstimateSolver::ConfigureModel(operations_research::RoutingModel &model,
                                        const std::shared_ptr<Printer> &printer,
                                        std::shared_ptr<const std::atomic<bool> > cancel_token,
                                        double cost_normalization_factor) {
        SolverWrapper::ConfigureModel(model, printer, cancel_token, cost_normalization_factor);

        AddTravelTime(model);
        AddVisitsHandling(model);
        AddSkillHandling(model);
        AddContinuityOfCare(model);
        AddCarerHandling(model);
        AddDroppedVisitsHandling(model);

        std::unordered_map<std::string, int> carer_to_vehicle;
        for (auto vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto &carer = Carer(vehicle);
            carer_to_vehicle[carer.sap_number()] = vehicle;
        }

        std::map<std::size_t, std::vector<int>> visit_vehicles;
        for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
            const auto &visit = problem_data_.NodeToVisit(visit_node);
            const auto visit_it = visit_vehicles.find(visit.id());
            if (visit_it != std::cend(visit_vehicles)) {
                continue;
            }

            const auto &carer_ids = human_planner_schedule_.find_visit_by_id(visit.id());
            for (const auto &carer_id  : carer_ids) {
                auto vehicle_it = carer_to_vehicle.find(carer_id);
                if (vehicle_it != std::cend(carer_to_vehicle)) {
                    visit_vehicles[visit.id()].push_back(vehicle_it->second);
                }
            }
        }

        std::map<std::size_t, std::size_t> visit_vehicle_index;
        for (auto &entry : visit_vehicles) {
            const auto entry_size = entry.second.size();
            CHECK_GE(entry_size, 0);
            CHECK_LE(entry_size, 2);

            if (entry_size == 2 && entry.second[0] > entry.second[1]) {
                std::swap(entry.second[0], entry.second[1]);
            }

            visit_vehicle_index[entry.first] = 0;
        }

        for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data_.nodes(); ++visit_node) {
            const auto &visit = problem_data_.NodeToVisit(visit_node);
            const auto &vehicles = visit_vehicles.at(visit.id());

            if (vehicles.empty()) {
                const auto index = index_manager_.NodeToIndex(visit_node);
                model.solver()->AddConstraint(model.solver()->MakeEquality(model.VehicleVar(index), -1));
            } else {
                const auto index = index_manager_.NodeToIndex(visit_node);
                const int vehicle_index = vehicles.at(visit_vehicle_index[visit.id()]++);
                model.solver()->AddConstraint(model.solver()->MakeMemberCt(model.VehicleVar(index), std::vector<int64>{-1, vehicle_index}));
            }
        }

        const auto schedule_day = GetScheduleDate();
        printer->operator<<(ProblemDefinition(model.vehicles(),
                                              model.nodes() - 1,
                                              "unknown area",
                                              schedule_day,
                                              visit_time_window_,
                                              break_time_window_,
                                              GetAdjustment()));

        VLOG(1) << "Finalizing definition of the routing model...";
        const auto start_time_model_closing = std::chrono::high_resolution_clock::now();

        model.CloseModelWithParameters(parameters_);

        const auto end_time_model_closing = std::chrono::high_resolution_clock::now();
        VLOG(1) << boost::format("Definition of the routing model finalized in %1% seconds")
                   % std::chrono::duration_cast<std::chrono::seconds>(end_time_model_closing
                                                                      - start_time_model_closing).count();

        auto solver_ptr = model.solver();
        model.AddSearchMonitor(solver_ptr->RevAlloc(new ProgressPrinterMonitor(model, printer, cost_normalization_factor)));
        model.AddSearchMonitor(solver_ptr->RevAlloc(new MinDroppedVisitsSolutionCollector(&model, true)));
        model.AddSearchMonitor(solver_ptr->RevAlloc(new CancelSearchLimit(cancel_token, solver_ptr)));

        if (!no_progress_time_limit_.is_special() && no_progress_time_limit_.total_seconds() > 0) {
            model.AddSearchMonitor(solver_ptr->RevAlloc(new StalledSearchLimit(
                    no_progress_time_limit_.total_milliseconds(),
                    &model,
                    model.solver()
            )));
        }
    }
}