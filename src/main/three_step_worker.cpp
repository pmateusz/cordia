#include <util/routing.h>
#include <ortools/constraint_solver/routing_parameters.h>
#include <absl/time/time.h>
#include <ortools/base/protoutil.h>

#include <utility>

#include "util/input.h"
#include "three_step_worker.h"
#include "metaheuristic_solver.h"
#include "multi_carer_solver.h"
#include "third_step_reduction_solver.h"
#include "delay_riskiness_reduction_solver.h"
#include "delay_probability_reduction_solver.h"
#include "delay_tracker.h"
#include "second_step_solver_no_expected_delay.h"
#include "declined_visit_evaluator.h"

void FailureInterceptor() {
    LOG(INFO) << "Failure";
}

rows::ThreeStepSchedulingWorker::CarerTeam::CarerTeam(std::pair<rows::Carer, rows::Diary> member)
        : diary_{member.second} {
    skills_ = member.first.skills();
    members_.emplace_back(std::move(member));
}

void rows::ThreeStepSchedulingWorker::CarerTeam::Add(std::pair<rows::Carer, rows::Diary> member) {
    DCHECK(std::find_if(std::begin(members_), std::end(members_),
                        [&member](const std::pair<rows::Carer, rows::Diary> &local_member) -> bool {
                            return local_member.first == member.first;
                        }) == std::end(members_));

    std::vector<int> common_skills;
    for (const auto skill : member.first.skills()) {
        if (std::find(std::cbegin(skills_), std::cend(skills_), skill) != std::cend(skills_)) {
            common_skills.emplace_back(skill);
        }
    }
    skills_ = common_skills;
    diary_ = diary_.Intersect(member.second);
    members_.emplace_back(std::move(member));
}

std::size_t rows::ThreeStepSchedulingWorker::CarerTeam::size() const {
    return members_.size();
}

std::vector<rows::Carer> rows::ThreeStepSchedulingWorker::CarerTeam::Members() const {
    std::vector<rows::Carer> result;
    for (const auto &member : members_) {
        result.push_back(member.first);
    }
    return result;
}

const std::vector<int> &rows::ThreeStepSchedulingWorker::CarerTeam::Skills() const {
    return skills_;
}

const std::vector<std::pair<rows::Carer, rows::Diary>> &
rows::ThreeStepSchedulingWorker::CarerTeam::FullMembers() const {
    return members_;
}

std::vector<rows::Carer>
rows::ThreeStepSchedulingWorker::CarerTeam::AvailableMembers(const boost::posix_time::ptime date_time,
                                                             const boost::posix_time::time_duration &adjustment) const {
    std::vector<rows::Carer> result;
    for (const auto &member : members_) {
        if (member.second.IsAvailable(date_time, adjustment)) {
            result.push_back(member.first);
        }
    }
    return result;
}

const rows::Diary &rows::ThreeStepSchedulingWorker::CarerTeam::Diary() const {
    return diary_;
}

int64 GetMaxDistance(rows::SolverWrapper &solver, const std::vector<std::vector<operations_research::RoutingNodeIndex>> &solution) {
    int64 max_distance = 0;
    for (const auto &route : solution) {
        const auto route_it_end = std::end(route);

        auto route_it = std::begin(route);
        if (route_it == route_it_end) {
            continue;
        }

        auto prev_node = *route_it;
        ++route_it;
        while (route_it != route_it_end) {
            auto node = *route_it;
            const auto current_distance = solver.Distance(prev_node, node);
            max_distance = std::max(max_distance, current_distance);

            prev_node = node;
            ++route_it;
        }
    }
    return max_distance;
}

void rows::ThreeStepSchedulingWorker::Run() {
    bool has_multiple_carer_visits = false;
    for (const auto &visit : problem_data_->problem().visits()) {
        CHECK_GT(visit.duration().total_seconds(), 0);
        if (visit.carer_count() > 1) {
            has_multiple_carer_visits = true;
        }
    }

    printer_->operator<<(TracingEvent(TracingEventType::Started, "All"));

    auto second_stage_search_params = operations_research::DefaultRoutingSearchParameters();
    second_stage_search_params.set_first_solution_strategy(operations_research::FirstSolutionStrategy_Value_PARALLEL_CHEAPEST_INSERTION);
    second_stage_search_params.mutable_local_search_operators()->set_use_full_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
    CHECK_OK(util_time::EncodeGoogleApiProto(absl::Seconds(30), second_stage_search_params.mutable_lns_time_limit()));
    second_stage_search_params.mutable_local_search_operators()->set_use_exchange_subtrip(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_relocate_expensive_chain(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_light_relocate_pair(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_relocate(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_exchange(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_exchange_pair(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_extended_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_node_pair_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_local_cheapest_insertion_path_lns(
            operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_local_cheapest_insertion_expensive_chain_lns(
            operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_global_cheapest_insertion_expensive_chain_lns(
            operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.mutable_local_search_operators()->set_use_global_cheapest_insertion_path_lns(
            operations_research::OptionalBoolean::BOOL_TRUE);
    second_stage_search_params.set_use_full_propagation(true);
//    second_stage_search_params.set_use_cp_sat(operations_research::OptionalBoolean::BOOL_TRUE);


    rows::SecondStepSolver second_stage_wrapper{*problem_data_,
                                                second_stage_search_params,
                                                visit_time_window_,
                                                break_time_window_,
                                                begin_end_shift_time_extension_,
                                                opt_time_limit_};

    if (second_stage_wrapper.vehicles() == 0) {
        LOG(FATAL) << "No carers available";
        printer_->operator<<(TracingEvent(TracingEventType::Finished, "All"));
        SetReturnCode(1);
        return;
    }

    std::vector<std::vector<int64>> second_step_initial_routes{static_cast<std::size_t>(second_stage_wrapper.vehicles())};
    if (first_stage_strategy_ != FirstStageStrategy::NONE && has_multiple_carer_visits) {
        LOG(INFO) << "Solving the first stage using " << GetAlias(first_stage_strategy_) << " strategy";

        second_step_initial_routes = SolveFirstStage(second_stage_wrapper);

        auto all_routes_empty = true;
        for (const auto &route: second_step_initial_routes) {
            if (!route.empty()) {
                all_routes_empty = false;
                break;
            }
        }

        LOG_IF(WARNING, all_routes_empty) << "First stage did not produce any routes";
    }

//    second_stage_model->solver()->set_fail_intercept(&FailureInterceptor);
    const auto routes = SolveSecondStage(second_step_initial_routes, second_stage_wrapper.index_manager(), second_stage_search_params);
    if (third_stage_strategy_ != ThirdStageStrategy::NONE) {
        LOG(INFO) << "Solving the third stage using " << GetAlias(third_stage_strategy_) << " strategy";
        SolveThirdStage(routes, second_stage_wrapper.index_manager());
    }

    printer_->operator<<(TracingEvent(TracingEventType::Finished, "All"));
    SetReturnCode(0);
}

rows::ThreeStepSchedulingWorker::ThreeStepSchedulingWorker(std::shared_ptr<rows::Printer> printer, std::shared_ptr<ProblemDataFactory> data_factory)
        : ThreeStepSchedulingWorker(std::move(printer), FirstStageStrategy::DEFAULT, ThirdStageStrategy::DEFAULT, data_factory) {}

rows::ThreeStepSchedulingWorker::ThreeStepSchedulingWorker(std::shared_ptr<rows::Printer> printer,
                                                           FirstStageStrategy first_stage_strategy,
                                                           ThirdStageStrategy third_stage_strategy,
                                                           std::shared_ptr<ProblemDataFactory> data_factory)
        : printer_{std::move(printer)},
          first_stage_strategy_{first_stage_strategy},
          third_stage_strategy_{third_stage_strategy},
          pre_opt_time_limit_{boost::posix_time::not_a_date_time},
          opt_time_limit_{boost::posix_time::not_a_date_time},
          post_opt_time_limit_{boost::posix_time::not_a_date_time},
          cost_normalization_factor_{1.0},
          data_factory_{std::move(data_factory)} {}

std::vector<rows::ThreeStepSchedulingWorker::CarerTeam> rows::ThreeStepSchedulingWorker::GetCarerTeams(const rows::Problem &problem) {
    std::vector<std::pair<rows::Carer, rows::Diary>> carer_diaries;
    for (const auto &carer_diary_pair : problem.carers()) {
        CHECK_EQ(carer_diary_pair.second.size(), 1);
        carer_diaries.emplace_back(carer_diary_pair.first, carer_diary_pair.second[0]);
    }

    std::sort(std::begin(carer_diaries), std::end(carer_diaries),
              [](const std::pair<rows::Carer, rows::Diary> &left,
                 const std::pair<rows::Carer, rows::Diary> &right) -> bool {
                  return left.second.duration() > right.second.duration();
              });

    std::vector<CarerTeam> teams;
    std::unordered_set<rows::Carer> processed_carers;
    const auto carer_diary_it_end = std::end(carer_diaries);
    for (auto carer_diary_it = std::begin(carer_diaries);
         carer_diary_it != carer_diary_it_end; ++carer_diary_it) {
        if (!processed_carers.insert(carer_diary_it->first).second) {
            continue;
        }

        CarerTeam team{*carer_diary_it};

        // while there is available space and are free carers continue looking for a suitable match
        boost::optional<std::pair<rows::Carer, rows::Diary>> best_match = boost::none;
        boost::optional<rows::Diary> best_match_diary = boost::none;
        boost::optional<int> best_match_shared_skills_num = boost::none;
        for (auto possible_match_it = std::next(carer_diary_it); possible_match_it != carer_diary_it_end; ++possible_match_it) {

            if (processed_carers.find(possible_match_it->first) != std::end(processed_carers)) {
                continue;
            }

            const auto match_diary = carer_diary_it->second.Intersect(possible_match_it->second);
            const auto match_shared_skills_num = possible_match_it->first.shared_skills(team.Skills()).size();
            if (!best_match_diary || (best_match_diary->duration() <= match_diary.duration()
                                      && *best_match_shared_skills_num <= match_shared_skills_num)) {
                best_match = *possible_match_it;
                best_match_diary = match_diary;
                best_match_shared_skills_num = match_shared_skills_num;
            }
        }

        if (best_match && best_match_diary->duration() >= boost::posix_time::time_duration(2, 15, 0, 0)) {
            if (!processed_carers.insert(best_match->first).second) {
                throw util::ApplicationError(
                        (boost::format("Carer %1% cannot be a member of more than 1 team")
                         % best_match->first).str(),
                        util::ErrorCode::ERROR);
            }
            team.Add(std::move(*best_match));

            CHECK_LE(team.Diary().begin_time(), team.Diary().end_time());

            teams.emplace_back(std::move(team));
        }
    }

    return teams;
}

bool rows::ThreeStepSchedulingWorker::Init(std::shared_ptr<const ProblemData> problem_data,
                                           std::shared_ptr<const rows::History> history,
                                           std::string output_file,
                                           boost::posix_time::time_duration visit_time_window,
                                           boost::posix_time::time_duration break_time_window,
                                           boost::posix_time::time_duration begin_end_shift_time_extension,
                                           boost::posix_time::time_duration pre_opt_time_limit,
                                           boost::posix_time::time_duration opt_time_limit,
                                           boost::posix_time::time_duration post_opt_time_limit,
                                           boost::optional<boost::posix_time::time_duration> time_limit,
                                           double cost_normalization_factor) {
    problem_data_ = std::move(problem_data);
    history_ = std::move(history);
    output_file_ = std::move(output_file);
    visit_time_window_ = std::move(visit_time_window);
    break_time_window_ = std::move(break_time_window);
    begin_end_shift_time_extension_ = std::move(begin_end_shift_time_extension);
    pre_opt_time_limit_ = std::move(pre_opt_time_limit);
    opt_time_limit_ = std::move(opt_time_limit);
    post_opt_time_limit_ = std::move(post_opt_time_limit);
    cost_normalization_factor_ = cost_normalization_factor;
    time_limit_ = std::move(time_limit);
    return true;
}

std::unique_ptr<rows::MetaheuristicSolver> rows::ThreeStepSchedulingWorker::CreateThirdStageSolver(
        const operations_research::RoutingSearchParameters &search_params,
        int64 max_dropped_visit_threshold) {
    CHECK_NE(third_stage_strategy_, ThirdStageStrategy::NONE);

    if (third_stage_strategy_ == ThirdStageStrategy::DEFAULT || third_stage_strategy_ == ThirdStageStrategy::DISTANCE) {
        return std::make_unique<rows::MetaheuristicSolver>(*problem_data_,
                                                           search_params,
                                                           visit_time_window_,
                                                           break_time_window_,
                                                           begin_end_shift_time_extension_,
                                                           post_opt_time_limit_,
                                                           max_dropped_visit_threshold);
    } else if (third_stage_strategy_ == ThirdStageStrategy::VEHICLE_REDUCTION) {
        return std::make_unique<rows::ThirdStepReductionSolver>(*problem_data_,
                                                                search_params,
                                                                visit_time_window_,
                                                                break_time_window_,
                                                                begin_end_shift_time_extension_,
                                                                post_opt_time_limit_,
                                                                max_dropped_visit_threshold);
    } else if (third_stage_strategy_ == ThirdStageStrategy::DELAY_RISKINESS_REDUCTION) {
        return std::make_unique<rows::DelayRiskinessReductionSolver>(*problem_data_,
                                                                     *history_,
                                                                     search_params,
                                                                     visit_time_window_,
                                                                     break_time_window_,
                                                                     begin_end_shift_time_extension_,
                                                                     post_opt_time_limit_,
                                                                     max_dropped_visit_threshold);
    } else {
        CHECK_EQ(third_stage_strategy_, ThirdStageStrategy::DELAY_PROBABILITY_REDUCTION);
        return std::make_unique<rows::DelayProbabilityReductionSolver>(*problem_data_,
                                                                       *history_,
                                                                       search_params,
                                                                       visit_time_window_,
                                                                       break_time_window_,
                                                                       begin_end_shift_time_extension_,
                                                                       post_opt_time_limit_,
                                                                       max_dropped_visit_threshold);
    }
}

operations_research::RoutingSearchParameters rows::ThreeStepSchedulingWorker::CreateThirdStageRoutingSearchParameters() {
    operations_research::RoutingSearchParameters parameters = operations_research::DefaultRoutingSearchParameters();
    parameters.set_first_solution_strategy(operations_research::FirstSolutionStrategy::PARALLEL_CHEAPEST_INSERTION);

// we can browse much more solutions in short time when these options are disabled
//    CHECK_OK(util_time::EncodeGoogleApiProto(absl::Seconds(1), parameters.mutable_lns_time_limit()));
//    parameters.mutable_local_search_operators()->set_use_full_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.set_use_full_propagation(true);

// testing
    parameters.mutable_local_search_operators()->set_use_exchange_subtrip(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_relocate_expensive_chain(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_light_relocate_pair(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_relocate(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_exchange(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_exchange_pair(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_extended_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_node_pair_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_tsp_lns(operations_research::OptionalBoolean::BOOL_TRUE);


// we are fishing for optimal solutions in scenarios in which all orders are performed
    parameters.mutable_local_search_operators()->set_use_cross_exchange(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_relocate_neighbors(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_full_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_local_cheapest_insertion_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_local_cheapest_insertion_expensive_chain_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_global_cheapest_insertion_expensive_chain_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_global_cheapest_insertion_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);

    parameters.set_local_search_metaheuristic(operations_research::LocalSearchMetaheuristic_Value_GUIDED_LOCAL_SEARCH);
    parameters.set_guided_local_search_lambda_coefficient(1.0);

// not worth as we don't usually deal with unperformed visits
    parameters.mutable_local_search_operators()->set_use_extended_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_relocate_and_make_active(operations_research::OptionalBoolean::BOOL_TRUE);

    if (time_limit_) {
        CHECK_OK(util_time::EncodeGoogleApiProto(absl::Seconds(time_limit_->total_seconds()), parameters.mutable_time_limit()));
    }

    return parameters;
}

std::vector<std::vector<int64>> rows::ThreeStepSchedulingWorker::SolveFirstStage(const rows::SolverWrapper &second_step_wrapper) {
    CHECK_NE(first_stage_strategy_, FirstStageStrategy::NONE);

    std::vector<std::vector<int64>> second_step_routes{static_cast<std::size_t>(second_step_wrapper.vehicles())};
    if (first_stage_strategy_ == FirstStageStrategy::DEFAULT || first_stage_strategy_ == FirstStageStrategy::TEAMS) {
        std::unordered_map<rows::Carer, CarerTeam> teams;
        int id = 0;
        std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > team_carers;
        for (auto &team : GetCarerTeams(problem_data_->problem())) {
            rows::Carer carer{(boost::format("team-%1%") % ++id).str(), rows::Transport::Foot, team.Skills()};

            if (team.size() > 1) {
                team_carers.push_back({carer, {team.Diary()}});
            }

            teams.emplace(std::move(carer), team);
        }

        std::vector<rows::CalendarVisit> team_visits;
        for (const auto &visit: problem_data_->problem().visits()) {
            if (visit.carer_count() > 1) {
                rows::CalendarVisit visit_copy{visit};
                visit_copy.carer_count(1);

                team_visits.emplace_back(std::move(visit_copy));
            }
        }
        CHECK(!team_visits.empty());

        operations_research::Assignment const *first_step_assignment = nullptr;

        auto search_params = operations_research::DefaultRoutingSearchParameters();
        search_params.set_first_solution_strategy(operations_research::FirstSolutionStrategy_Value_PARALLEL_CHEAPEST_INSERTION);
        search_params.use_cp();

        rows::Problem sub_problem{team_visits, team_carers, problem_data_->problem().service_users()};
        const auto sub_problem_data = data_factory_->makeProblem(sub_problem);
        rows::SingleStepSolver first_stage_wrapper{*sub_problem_data,
                                                   search_params,
                                                   visit_time_window_,
                // break time window is 0 for teams, because their breaks have to be synchronized
                                                   boost::posix_time::seconds(0),
                                                   boost::posix_time::not_a_date_time,
                                                   pre_opt_time_limit_};
        operations_research::RoutingModel first_step_model{first_stage_wrapper.index_manager()};
        first_stage_wrapper.ConfigureModel(first_step_model, printer_, CancelToken(), cost_normalization_factor_);

        printer_->operator<<(TracingEvent(TracingEventType::Started, "Stage1"));
//        first_step_model->solver()->set_fail_intercept(&FailureInterceptor);
        first_step_assignment = first_step_model.SolveWithParameters(search_params);
        printer_->operator<<(TracingEvent(TracingEventType::Finished, "Stage1"));

        if (first_step_assignment == nullptr) {
            throw util::ApplicationError("No first stage solution found.", util::ErrorCode::ERROR);
        }

        operations_research::Assignment first_validation_copy{first_step_assignment};
        const auto is_first_solution_correct = first_step_model.solver()->CheckAssignment(&first_validation_copy);
        DCHECK(is_first_solution_correct);

        std::vector<std::vector<int64>> first_step_solution;
        first_step_model.AssignmentToRoutes(*first_step_assignment, &first_step_solution);

        auto time_dim = first_step_model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
        auto route_number = 0;
        for (const auto &route : first_step_solution) {
            const auto &team_carer = first_stage_wrapper.Carer(route_number);
            const auto team_carer_find_it = teams.find(team_carer);
            DCHECK(team_carer_find_it != std::end(teams));

            for (const auto node_index : route) {
                const auto &visit = first_stage_wrapper.NodeToVisit(first_stage_wrapper.index_manager().IndexToNode(node_index));

                std::vector<int> vehicle_numbers;
                boost::posix_time::ptime visit_start_time{
                        team_carer_find_it->second.Diary().date(),
                        boost::posix_time::seconds(
                                first_step_assignment->Min(time_dim->CumulVar(node_index)))
                };

                for (const auto &carer : team_carer_find_it->second.AvailableMembers(visit_start_time, first_stage_wrapper.GetAdjustment())) {
                    vehicle_numbers.push_back(second_step_wrapper.Vehicle(carer));
                }

                DCHECK_EQ(vehicle_numbers.size(), 2);
                DCHECK_NE(vehicle_numbers[0], vehicle_numbers[1]);

                const auto &visit_nodes = second_step_wrapper.GetNodes(visit);
                std::vector<int64> visit_indices_to_use = second_step_wrapper.index_manager().NodesToIndices(visit_nodes);
                DCHECK_EQ(visit_indices_to_use.size(), vehicle_numbers.size());


                auto first_vehicle_to_use = vehicle_numbers[0];
                auto second_vehicle_to_use = vehicle_numbers[1];
                if (first_vehicle_to_use > second_vehicle_to_use) {
                    std::swap(first_vehicle_to_use, second_vehicle_to_use);
                }

                auto first_visit_to_use = visit_indices_to_use[0];
                auto second_visit_to_use = visit_indices_to_use[1];
                if (first_visit_to_use > second_visit_to_use) {
                    std::swap(first_visit_to_use, second_visit_to_use);
                }

                second_step_routes[first_vehicle_to_use].push_back(first_visit_to_use);
                second_step_routes[second_vehicle_to_use].push_back(second_visit_to_use);
            }

            ++route_number;
        }
    } else if (first_stage_strategy_ == FirstStageStrategy::SOFT_TIME_WINDOWS) {
        auto internal_search_params = operations_research::DefaultRoutingSearchParameters();
        internal_search_params.set_first_solution_strategy(
                operations_research::FirstSolutionStrategy_Value_AUTOMATIC); // do not use cp_sat but sweeping
//        internal_search_params.set_first_solution_strategy(
//                operations_research::FirstSolutionStrategy_Value_SAVINGS); // do not use cp_sat but sweeping
//        internal_search_params.set_savings_max_memory_usage_bytes(1024.0 * 1024.0 * 1024.0);
        internal_search_params.set_savings_parallel_routes(true);
        internal_search_params.set_use_full_propagation(true);
        internal_search_params.mutable_local_search_operators()->set_use_cross_exchange(operations_research::BOOL_TRUE);
        internal_search_params.mutable_local_search_operators()->set_use_relocate_neighbors(operations_research::BOOL_TRUE);
        internal_search_params.set_local_search_metaheuristic(operations_research::LocalSearchMetaheuristic_Value_TABU_SEARCH);

        std::vector<rows::CalendarVisit> team_visits;
        for (const auto &visit: problem_data_->problem().visits()) {
            if (visit.carer_count() > 1) {
                CHECK_EQ(visit.carer_count(), 2);
                team_visits.emplace_back(visit);
            }
        }

        rows::Problem sub_problem{team_visits, problem_data_->problem().carers(), problem_data_->problem().service_users()};
        const auto sub_problem_data = data_factory_->makeProblem(sub_problem);
        rows::MultiCarerSolver multi_carer_wrapper{*sub_problem_data,
                                                   internal_search_params,
                                                   visit_time_window_,
                                                   break_time_window_,
                                                   begin_end_shift_time_extension_,
                                                   pre_opt_time_limit_};
        operations_research::RoutingModel multi_carer_model{multi_carer_wrapper.index_manager()};
        multi_carer_wrapper.ConfigureModel(multi_carer_model, printer_, CancelToken(), cost_normalization_factor_);
        auto result = multi_carer_model.SolveWithParameters(internal_search_params);
        operations_research::Assignment const *multi_carer_assignment = multi_carer_wrapper.GetBestSolution();
        if (multi_carer_assignment == nullptr) {
            multi_carer_assignment = result;
        }

        if (multi_carer_assignment == nullptr) {
            throw util::ApplicationError("No first stage solution found.", util::ErrorCode::ERROR);
        }

        std::vector<std::vector<int64>> multi_carer_solution;
        multi_carer_model.AssignmentToRoutes(*multi_carer_assignment, &multi_carer_solution);

        auto time_dim = multi_carer_model.GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
        auto route_number = 0;
        std::unordered_set<int64> used_visit_indices;
        for (const auto &route : multi_carer_solution) {
            const auto &carer = multi_carer_wrapper.Carer(route_number);

            for (const auto node_index : route) {
                const auto &visit = multi_carer_wrapper.NodeToVisit(multi_carer_wrapper.index_manager().IndexToNode(node_index));

                const auto &visit_nodes = second_step_wrapper.GetNodes(visit);
                std::vector<int64> visit_indices = second_step_wrapper.index_manager().NodesToIndices(visit_nodes);
                CHECK_EQ(visit_indices.size(), 2);

                int64 visit_index_to_use = -1;
                for (auto visit_index : visit_indices) {
                    if (used_visit_indices.find(visit_index) == used_visit_indices.end()) {
                        visit_index_to_use = visit_index;
                        break;
                    }
                }
                CHECK_NE(visit_index_to_use, -1);
                used_visit_indices.emplace(visit_index_to_use);

                const auto multi_carer_visit_nodes = multi_carer_wrapper.GetNodes(visit);
                const auto multi_carer_visit_indices = multi_carer_wrapper.index_manager().NodesToIndices(multi_carer_visit_nodes);
                const auto difference_between_values = multi_carer_assignment->Value(time_dim->CumulVar(multi_carer_visit_indices.at(0)))
                                                       - multi_carer_assignment->Value(time_dim->CumulVar(multi_carer_visit_indices.at(1)));
                if (difference_between_values == 0) { // time difference during arrival is zero hence it is synchronized
                    second_step_routes[route_number].push_back(visit_index_to_use);
                }
            }

            ++route_number;
        }
    }

    return second_step_routes;
}

int64 GetEssentialRiskiness(std::vector<int64> delays) {
    std::sort(std::begin(delays), std::end(delays));

    // if last element is negative then index is zero
    const auto num_delays = delays.size();
    int64 delay_pos = num_delays - 1;
    if (delays.at(delay_pos) <= 0) {
        return 0;
    }

//    if (delays.at(0) >= 0) {
//        return kint64max;
//    }

    // compute total delay
    int64 total_delay = 0;
    for (; delay_pos >= 0 && delays.at(delay_pos) >= 0; --delay_pos) {
        total_delay += delays.at(delay_pos);
    }
    CHECK_GT(total_delay, 0);
//    CHECK_GT(delay_pos, 0);

//    if (delays.at(0) >= 0) {
    if (delay_pos == -1) {
        return total_delay;
        return kint64max;
    }

    // find minimum traffic index that compensates the total delay
    int64 delay_budget = 0;
    for (; delay_pos > 0 && delay_budget + (delay_pos + 1) * delays.at(delay_pos) + total_delay > 0; --delay_pos) {
        delay_budget += delays.at(delay_pos);
    }

    int64 delay_balance = delay_budget + (delay_pos + 1) * delays.at(delay_pos) + total_delay;
    if (delay_balance < 0) {
        int64 riskiness_index = std::min(0l, delays.at(delay_pos + 1));
        CHECK_LE(riskiness_index, 0);

        int64 remaining_balance = total_delay + delay_budget + (delay_pos + 1) * riskiness_index;
        CHECK_GE(remaining_balance, 0);

        riskiness_index -= std::ceil(static_cast<double>(remaining_balance) / static_cast<double>(delay_pos + 1));
        CHECK_LE(riskiness_index * (delay_pos + 1) + delay_budget + total_delay, 0);

        return -riskiness_index;
    } else if (delay_balance > 0) {
        CHECK_EQ(delay_pos, 0);
        return delay_balance;
        return kint64max;
    }

    return delays.at(delay_pos);
}

class NodeMeanDelayRemover {
public:
    NodeMeanDelayRemover(rows::SolverWrapper &solver_wrapper,
                         operations_research::RoutingModel &model,
                         const rows::History &history)
            : solver_wrapper_{solver_wrapper},
              model_{model},
              history_{history} {}

    std::vector<std::vector<int64>> RemoveDelayedNodes(const std::vector<std::vector<int64>> &routes) {
        rows::DelayTracker local_tracker{solver_wrapper_,
                                         history_,
                                         &model_.GetDimensionOrDie(rows::SolverWrapper::TIME_DIMENSION)};

        std::unordered_set<int64> nodes_to_skip;
        operations_research::Assignment *restored_assignment = nullptr;
        int iteration = 0;
        while (restored_assignment == nullptr && iteration < 64) {
            ++iteration;

//            if (iteration > 1) {
//                model_.solver()->set_fail_intercept(&FailureInterceptor);
//            }

            auto filtered_routes = GetFilteredRoutes(nodes_to_skip, routes);
            solver_wrapper_.ClearFailedIndices();
            auto filtered_assignment = model_.ReadAssignmentFromRoutes(filtered_routes, false);

            // candidates to remove should not have a predecessor on a path which is also a candidate to remove
            std::unordered_set<int64> candidates_to_remove = solver_wrapper_.FailedIndices();
            CHECK (filtered_assignment != nullptr || !candidates_to_remove.empty());

            const auto indices_to_remove = NominateCandidatesToRemove(candidates_to_remove, filtered_routes);
            for (const auto index : indices_to_remove) {
                nodes_to_skip.emplace(index);
            }

            if (filtered_assignment != nullptr) {
                bool extra_nodes_removed = false;
                restored_assignment = model_.RestoreAssignment(*filtered_assignment);
                local_tracker.UpdateAllPaths(restored_assignment);

                std::unordered_set<int64> next_candidates_to_remove;
                for (auto index = 0; index < solver_wrapper_.index_manager().num_indices(); ++index) {
                    if (!local_tracker.IsVisited(index)) { continue; }
                    if (model_.IsStart(index) || model_.IsEnd(index)) { continue; }

//                    const auto node = filtered_second_stage_wrapper.index_manager().IndexToNode(index);
//                    const auto visit = filtered_second_stage_wrapper.NodeToVisit(node);
//                    if (visit.id() == 8482342) {
//                        const auto mean_delay = local_tracker.GetMeanDelay(index);
//                        local_tracker.PrintPath(8482342);
//                        LOG(INFO) << "Mean delay (8482342): " << mean_delay;
//                    }

                    const auto mean_delay = local_tracker.GetMeanDelay(index);
                    if (mean_delay > 0) {
                        const auto node = solver_wrapper_.index_manager().IndexToNode(index);
                        const auto &visit = solver_wrapper_.NodeToVisit(node);
                        if (visit.carer_count() == 1) {
                            next_candidates_to_remove.emplace(index);
                        } else {
                            const auto nodes = solver_wrapper_.GetNodePair(visit);
                            const auto first_index = solver_wrapper_.index_manager().NodeToIndex(nodes.first);
                            const auto second_index = solver_wrapper_.index_manager().NodeToIndex(nodes.second);
                            if (candidates_to_remove.find(first_index) == std::cend(candidates_to_remove)
                                && candidates_to_remove.find(second_index) == std::cend(candidates_to_remove)) {
                                next_candidates_to_remove.emplace(first_index);
                                next_candidates_to_remove.emplace(second_index);
                            }
                        }
                    }
                }

                if (!next_candidates_to_remove.empty()) {
                    const auto next_indices_to_remove = NominateCandidatesToRemove(next_candidates_to_remove, filtered_routes);
                    CHECK(!next_candidates_to_remove.empty());

                    for (const auto index : next_candidates_to_remove) {
                        nodes_to_skip.emplace(index);

//                        const auto node = solver_wrapper_.index_manager().IndexToNode(index);
//                        const auto &visit = solver_wrapper_.NodeToVisit(node);
//                        local_tracker.PrintPath(visit.id());
                    }

                    extra_nodes_removed = true;
                    restored_assignment = nullptr;
                }
            }
        }

        return GetFilteredRoutes(nodes_to_skip, routes);
    }

private:
    std::vector<std::vector<int64>> GetFilteredRoutes(const std::unordered_set<int64> &nodes_to_remove,
                                                      const std::vector<std::vector<int64>> &routes) {
        std::vector<std::vector<int64>> filtered_routes;
        for (const auto &tour : routes) {
            std::vector<int64> filtered_route;
            for (const auto node : tour) {
                if (nodes_to_remove.find(node) == std::cend(nodes_to_remove)) {
                    filtered_route.emplace_back(node);
                }
            }
            filtered_routes.emplace_back(std::move(filtered_route));
        }
        return filtered_routes;
    }

    std::unordered_set<int64> NominateCandidatesToRemove(std::unordered_set<int64> candidates_to_remove,
                                                         const std::vector<std::vector<int64>> &routes) {
        std::unordered_set<int64> nodes_to_remove;

        if (candidates_to_remove.empty()) {
            return nodes_to_remove;
        }

        for (std::size_t tour_index = 0; tour_index < routes.size(); ++tour_index) {
            const auto &tour = routes.at(tour_index);
            const auto tour_length = tour.size();

            std::size_t pos = 0;
            for (; pos < tour_length; ++pos) {
                if (candidates_to_remove.find(tour.at(pos)) != std::cend(candidates_to_remove)) {
                    break;
                }
            }
            ++pos;

            for (; pos < tour_length; ++pos) {
                auto index_it = candidates_to_remove.find(tour.at(pos));
                if (index_it != std::cend(candidates_to_remove)) {
                    candidates_to_remove.erase(index_it);
                }
            }
        }

        for (const auto index : candidates_to_remove) {
            const auto node = solver_wrapper_.index_manager().IndexToNode(index);
            const auto &visit = solver_wrapper_.NodeToVisit(node);
            if (visit.carer_count() == 1) {
                nodes_to_remove.emplace(index);
            } else {
                const auto nodes = solver_wrapper_.GetNodePair(visit);
                const auto first_index = solver_wrapper_.index_manager().NodeToIndex(nodes.first);
                const auto second_index = solver_wrapper_.index_manager().NodeToIndex(nodes.second);
                if (candidates_to_remove.find(first_index) != std::cend(candidates_to_remove)
                    && candidates_to_remove.find(second_index) != std::cend(candidates_to_remove)) {
                    nodes_to_remove.emplace(first_index);
                    nodes_to_remove.emplace(second_index);
                }
            }
        }

        return nodes_to_remove;
    }

    rows::SolverWrapper &solver_wrapper_;
    operations_research::RoutingModel &model_;
    const rows::History &history_;
};

std::vector<std::vector<int64> > rows::ThreeStepSchedulingWorker::SolveSecondStage(
        const std::vector<std::vector<int64>> &second_stage_initial_routes,
        const operations_research::RoutingIndexManager &index_manager,
        const operations_research::RoutingSearchParameters &search_params) {
    static const auto LOAD_DEBUG_FILES = false;
    static const auto SOLUTION_EXTENSION = ".bin";

    const std::string SECOND_STAGE_XML_SOLUTION = "";

    rows::SecondStepSolver second_stage_wrapper{*problem_data_,
                                                search_params,
                                                visit_time_window_,
                                                break_time_window_,
                                                begin_end_shift_time_extension_,
                                                opt_time_limit_};
    const auto scheduling_day = second_stage_wrapper.GetScheduleDate();

    std::stringstream second_stage_solution_name;
    second_stage_solution_name << "assignment_" << boost::gregorian::to_simple_string(scheduling_day) << SOLUTION_EXTENSION;

    std::stringstream filtered_second_stage_solution_name;
    filtered_second_stage_solution_name << "filtered_assignment_" << boost::gregorian::to_simple_string(scheduling_day) << SOLUTION_EXTENSION;

    const auto assignment_save_copy = second_stage_solution_name.str();
    const auto final_assignment_save_copy = filtered_second_stage_solution_name.str();

    std::unique_ptr<operations_research::RoutingModel> second_stage_model = std::make_unique<operations_research::RoutingModel>(index_manager);
//    second_stage_model->solver()->set_fail_intercept(&FailureInterceptor);
    second_stage_wrapper.ConfigureModel(*second_stage_model, printer_, CancelToken(), cost_normalization_factor_);

//    for (auto index = 0; index < second_stage_solver.index_manager().num_indices(); ++index) {
//        const auto routing_node = second_stage_solver.index_manager().IndexToNode(index);
//        if (routing_node == rows::RealProblemData::DEPOT) { continue; }
//
//        const auto &visit = second_stage_solver.NodeToVisit(routing_node);
//        if (visit.id() == 8559516) {
//            LOG(INFO) << index;
//        }
//    }

    operations_research::Assignment const *second_stage_assignment = nullptr;
    operations_research::Assignment const *filtered_assignment = nullptr;

    operations_research::RoutingModel filtered_second_stage_model{index_manager};
    rows::SecondStepSolverNoExpectedDelay filtered_second_stage_wrapper{*problem_data_,
                                                                        *history_,
                                                                        search_params,
                                                                        visit_time_window_,
                                                                        break_time_window_,
                                                                        begin_end_shift_time_extension_,
                                                                        opt_time_limit_};
    filtered_second_stage_wrapper.ConfigureModel(filtered_second_stage_model, printer_, CancelToken(), cost_normalization_factor_);

    if (LOAD_DEBUG_FILES && boost::filesystem::is_regular_file(final_assignment_save_copy)) {
        filtered_assignment = filtered_second_stage_model.ReadAssignment(final_assignment_save_copy);
        std::vector<std::vector<int64>> routes;
        filtered_second_stage_model.AssignmentToRoutes(*filtered_assignment, &routes);
        return routes;
    }

    if (!SECOND_STAGE_XML_SOLUTION.empty() && boost::filesystem::is_regular_file(SECOND_STAGE_XML_SOLUTION)) {
        rows::Solution::XmlLoader loader;
        const auto xml_solution = loader.Load(SECOND_STAGE_XML_SOLUTION);
        const auto routes = second_stage_wrapper.GetRoutes(xml_solution, *second_stage_model);
        second_stage_assignment = second_stage_model->ReadAssignmentFromRoutes(routes, false);
        CHECK(second_stage_assignment) << "Failed to load: " << SECOND_STAGE_XML_SOLUTION;
    } else if (LOAD_DEBUG_FILES && boost::filesystem::is_regular_file(assignment_save_copy)) {
        second_stage_assignment = second_stage_model->ReadAssignment(assignment_save_copy);
        CHECK(second_stage_assignment) << "Failed to load: " << assignment_save_copy;
    } else {
        std::stringstream penalty_msg;
        penalty_msg << "MissedVisitPenalty: " << second_stage_wrapper.GetDroppedVisitPenalty();
        printer_->operator<<(TracingEvent(TracingEventType::Unknown, penalty_msg.str()));

        const auto second_stage_initial_assignment = second_stage_model->ReadAssignmentFromRoutes(second_stage_initial_routes, true);
        printer_->operator<<(TracingEvent(TracingEventType::Started, "Stage2"));
        second_stage_assignment = second_stage_model->SolveFromAssignmentWithParameters(second_stage_initial_assignment, search_params);
        printer_->operator<<(TracingEvent(TracingEventType::Finished, "Stage2"));

        if (second_stage_assignment == nullptr) {
            throw util::ApplicationError("No second stage solution found.", util::ErrorCode::ERROR);
        }

        if (LOAD_DEBUG_FILES) {
            const auto save_status = second_stage_model->WriteAssignment(assignment_save_copy);
            CHECK(save_status);
            LOG(INFO) << "Second stage assignment written to file: " << assignment_save_copy;
        }

        for (int vehicle = 0; vehicle < second_stage_model->vehicles(); ++vehicle) {
            const auto validation_result = solution_validator_.ValidateFull(vehicle,
                                                                            *second_stage_assignment,
                                                                            *second_stage_model,
                                                                            second_stage_wrapper);
            CHECK(validation_result.error() == nullptr);
        }

        boost::filesystem::path output_path{output_file_};
        std::string second_stage_output_file{"second_stage_"};
        second_stage_output_file += output_path.filename().string();
        boost::filesystem::path second_stage_output = boost::filesystem::absolute(second_stage_output_file, output_path.parent_path());

        solution_writer_.Write(second_stage_output,
                               second_stage_wrapper,
                               *second_stage_model,
                               *second_stage_assignment);
    }

    if (third_stage_strategy_ != ThirdStageStrategy::DELAY_RISKINESS_REDUCTION
        && third_stage_strategy_ != ThirdStageStrategy::DELAY_PROBABILITY_REDUCTION) {
        std::vector<std::vector<int64>> routes;
        second_stage_model->AssignmentToRoutes(*second_stage_assignment, &routes);
        return routes;
    }

//  second_stage_model.solver()->set_fail_intercept(FailureInterceptor);
//  const auto solution_assignment = second_stage_model->ReadAssignmentFromRoutes(solution, false);
    CHECK(second_stage_assignment != nullptr);

//    for (int vehicle = 0; vehicle < index_manager.num_vehicles(); ++vehicle) {
//        int64 current_index = delay_tracker.Record(second_stage_model->Start(vehicle)).next;
//        while (!second_stage_model->IsEnd(current_index)) {
//            if (delay_tracker.GetMeanDelay(current_index) > 0) {
//                nodes_to_skip.emplace(current_index);
//            }
//
//            current_index = delay_tracker.Record(current_index).next;
//        }
//    }

    std::vector<std::vector<int64>> routes;
    second_stage_model->AssignmentToRoutes(*second_stage_assignment, &routes);

    NodeMeanDelayRemover delayed_node_remover{filtered_second_stage_wrapper, filtered_second_stage_model, *history_};
    const auto filtered_routes = delayed_node_remover.RemoveDelayedNodes(routes);

    filtered_assignment = filtered_second_stage_model.ReadAssignmentFromRoutes(filtered_routes, false);
    CHECK(filtered_assignment != nullptr);

    if (LOAD_DEBUG_FILES) {
        solution_writer_.Write("filtered_solution.gexf", filtered_second_stage_wrapper, filtered_second_stage_model, *filtered_assignment);
    }

    printer_->operator<<(TracingEvent(TracingEventType::Started, "Stage2-Patch"));
    auto valid_second_stage_assignment
            = filtered_second_stage_model.SolveFromAssignmentWithParameters(filtered_assignment, search_params);
    if (valid_second_stage_assignment == nullptr) {
        throw util::ApplicationError("No second stage patched solution found.", util::ErrorCode::ERROR);
    }

    if (LOAD_DEBUG_FILES) {
        const auto save_status = filtered_second_stage_model.WriteAssignment(final_assignment_save_copy);
        CHECK(save_status);
        LOG(INFO) << "Patched second stage assignment written to file: " << final_assignment_save_copy;
    }

    printer_->operator<<(TracingEvent(TracingEventType::Finished, "Stage2-Patch"));
    routes.clear();
    filtered_second_stage_model.AssignmentToRoutes(*valid_second_stage_assignment, &routes);
    return routes;
}

void rows::ThreeStepSchedulingWorker::WriteSolution(const operations_research::Assignment *assignment,
                                                    const operations_research::RoutingModel &model,
                                                    const SolverWrapper &solver) const {
    operations_research::Assignment *assignment_copy = model.solver()->MakeAssignment(assignment);
    const auto is_third_solution_correct = model.solver()->CheckAssignment(assignment_copy);
    DCHECK(is_third_solution_correct);

    for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        const auto validation_result = solution_validator_.ValidateFull(vehicle, *assignment, model, solver);
        CHECK(validation_result.error() == nullptr);
    }

    solution_writer_.Write(output_file_, solver, model, *assignment);
}

int64 GetEssentialRiskiness(int64 num_indices, rows::DelayTracker &tracker, const operations_research::Assignment *assignment) {
    if (num_indices <= 0) { return 0; }

    tracker.UpdateAllPaths(assignment);
    std::vector<int64> element_riskiness;
    for (auto index = 0; index < num_indices; ++index) {
        if (!tracker.IsVisited(index)) {
            continue;
        }

//        const auto mean_delay = tracker.GetMeanDelay(index);
//        if (mean_delay > 0) {
//            LOG(INFO) << "HERE";
//        }

        int64 local_riskiness = tracker.GetEssentialRiskiness(index);
        element_riskiness.push_back(local_riskiness);
    }

    return *std::max_element(std::cbegin(element_riskiness), std::cend(element_riskiness));
}

void rows::ThreeStepSchedulingWorker::SolveThirdStage(const std::vector<std::vector<int64>> &second_stage_routes,
                                                      const operations_research::RoutingIndexManager &index_manager) {
    DeclinedVisitEvaluator declined_evaluator{*problem_data_, index_manager};

    const auto third_search_params = CreateThirdStageRoutingSearchParameters();
    if (third_stage_strategy_ == ThirdStageStrategy::DELAY_RISKINESS_REDUCTION) {
        operations_research::RoutingModel pre_assignment_third_stage_model{index_manager};
        rows::DelayRiskinessReductionSolver pre_assignment_third_step_solver(*problem_data_,
                                                                             *history_,
                                                                             third_search_params,
                                                                             visit_time_window_,
                                                                             break_time_window_,
                                                                             begin_end_shift_time_extension_,
                                                                             post_opt_time_limit_,
                                                                             1);
        pre_assignment_third_step_solver.ConfigureModel(pre_assignment_third_stage_model, printer_, CancelToken(), cost_normalization_factor_);

        NodeMeanDelayRemover delayed_node_remover{pre_assignment_third_step_solver, pre_assignment_third_stage_model, *history_};
        const auto filtered_routes = delayed_node_remover.RemoveDelayedNodes(second_stage_routes);


        const auto max_dropped_visit_threshold = declined_evaluator.GetThreshold(filtered_routes);

        operations_research::RoutingModel third_stage_model{index_manager};
        rows::DelayRiskinessReductionSolver third_step_solver(*problem_data_,
                                                              *history_,
                                                              third_search_params,
                                                              visit_time_window_,
                                                              break_time_window_,
                                                              begin_end_shift_time_extension_,
                                                              post_opt_time_limit_,
                                                              max_dropped_visit_threshold);
        third_step_solver.ConfigureModel(third_stage_model, printer_, CancelToken(), cost_normalization_factor_);
        operations_research::Assignment *third_stage_pre_assignment = third_stage_model.ReadAssignmentFromRoutes(filtered_routes, false);
        DCHECK(third_stage_pre_assignment != nullptr);

        rows::DelayTracker local_tracker{third_step_solver,
                                         *history_,
                                         &third_stage_model.GetDimensionOrDie(rows::SolverWrapper::TIME_DIMENSION)};
        const auto max_pre_riskiness = GetEssentialRiskiness(third_step_solver.index_manager().num_indices(),
                                                             local_tracker,
                                                             third_stage_pre_assignment);
        WriteSolution(third_stage_pre_assignment, third_stage_model, third_step_solver);

        printer_->operator<<(TracingEvent(TracingEventType::Started, "Stage3"));
        const operations_research::Assignment *third_stage_assignment
                = third_stage_model.SolveFromAssignmentWithParameters(third_stage_pre_assignment, third_search_params);
        if (third_stage_assignment == nullptr) {
            throw util::ApplicationError("No third stage solution found.", util::ErrorCode::ERROR);
        }

        const auto max_post_riskiness = GetEssentialRiskiness(third_step_solver.index_manager().num_indices(),
                                                              local_tracker,
                                                              third_stage_assignment);
        printer_->operator<<(TracingEvent(TracingEventType::Finished, "Stage3"));

        if (max_pre_riskiness > max_post_riskiness) {
            WriteSolution(third_stage_assignment, third_stage_model, third_step_solver);
        }
    } else {
        const auto max_dropped_visit_threshold = declined_evaluator.GetThreshold(second_stage_routes);

        operations_research::RoutingModel third_stage_model{index_manager};
        std::unique_ptr<MetaheuristicSolver> third_step_solver = CreateThirdStageSolver(third_search_params, max_dropped_visit_threshold);
        third_step_solver->ConfigureModel(third_stage_model, printer_, CancelToken(), cost_normalization_factor_);
        operations_research::Assignment *third_stage_pre_assignment = third_stage_model.ReadAssignmentFromRoutes(second_stage_routes, true);
        DCHECK(third_stage_pre_assignment != nullptr);

        printer_->operator<<(TracingEvent(TracingEventType::Started, "Stage3"));
        const operations_research::Assignment *third_stage_assignment = third_stage_model.SolveFromAssignmentWithParameters(
                third_stage_pre_assignment, third_search_params);
        if (third_stage_assignment == nullptr) {
            throw util::ApplicationError("No third stage solution found.", util::ErrorCode::ERROR);
        }

        printer_->operator<<(TracingEvent(TracingEventType::Finished, "Stage3"));
        WriteSolution(third_stage_assignment, third_stage_model, *third_step_solver);
    }
}

std::vector<rows::RouteValidatorBase::Metrics> rows::ThreeStepSchedulingWorker::GetVehicleMetrics(const std::vector<std::vector<int64>> &routes,
                                                                                                  const rows::SolverWrapper &second_stage_wrapper) {
    rows::SecondStepSolver intermediate_wrapper{*problem_data_,
                                                operations_research::DefaultRoutingSearchParameters(),
                                                visit_time_window_,
                                                break_time_window_,
                                                begin_end_shift_time_extension_,
                                                boost::date_time::not_a_date_time};
    operations_research::RoutingModel intermediate_model{second_stage_wrapper.index_manager()};
    intermediate_wrapper.ConfigureModel(intermediate_model, printer_, CancelToken(), cost_normalization_factor_);

// useful for debugging
//    const auto warm_start_solution = util::LoadSolution(
//            "/home/pmateusz/dev/cordia/simulations/current_review_simulations/benchmark/25/solutions/solution_20171001_v25m0c3_mip.gexf",
//            problem_, visit_time_window_);
//
//    const auto routes = intermediate_wrapper->GetRoutes(warm_start_solution, *intermediate_index_manager, *intermediate_model);

    auto assignment_to_use = intermediate_model.ReadAssignmentFromRoutes(routes, true);
    CHECK(assignment_to_use);
    std::vector<rows::RouteValidatorBase::Metrics> vehicle_metrics;
    for (int vehicle = 0; vehicle < intermediate_model.vehicles(); ++vehicle) {
        const auto validation_result = solution_validator_.ValidateFull(vehicle, *assignment_to_use, intermediate_model, intermediate_wrapper);
        CHECK(validation_result.error() == nullptr);
        vehicle_metrics.emplace_back(validation_result.metrics());
    }

    return vehicle_metrics;
}

boost::optional<rows::FirstStageStrategy> rows::ParseFirstStageStrategy(const std::string &value) {
    const auto value_to_use = boost::algorithm::to_lower_copy(value);

    if (value == "default") {
        return boost::make_optional(FirstStageStrategy::DEFAULT);
    }

    if (value == "none") {
        return boost::make_optional(FirstStageStrategy::NONE);
    }

    if (value == "teams") {
        return boost::make_optional(FirstStageStrategy::TEAMS);
    }

    if (value == "soft-time-windows") {
        return boost::make_optional(FirstStageStrategy::SOFT_TIME_WINDOWS);
    }

    return boost::none;
}

rows::FirstStageStrategy rows::GetAlias(rows::FirstStageStrategy strategy) {
    if (strategy == FirstStageStrategy::DEFAULT) {
        return FirstStageStrategy::TEAMS;
    }
    return strategy;
}

std::ostream &rows::operator<<(std::ostream &out, rows::FirstStageStrategy value) {
    switch (value) {
        case FirstStageStrategy::TEAMS:
            out << "TEAMS";
            break;
        case FirstStageStrategy::DEFAULT:
            out << "DEFAULT";
            break;
        case FirstStageStrategy::SOFT_TIME_WINDOWS:
            out << "SOFT_TIME_WINDOWS";
            break;
        case FirstStageStrategy::NONE:
            out << "NONE";
            break;
        default:
            LOG(FATAL) << "Translation of value " << static_cast<int>(value) << " to string is not implemented";
    }
    return out;
}

boost::optional<rows::ThirdStageStrategy> rows::ParseThirdStageStrategy(const std::string &value) {
    const auto value_to_use = boost::algorithm::to_lower_copy(value);

    if (value == "default") {
        return boost::make_optional(ThirdStageStrategy::DEFAULT);
    }

    if (value == "none") {
        return boost::make_optional(ThirdStageStrategy::NONE);
    }

    if (value == "distance") {
        return boost::make_optional(ThirdStageStrategy::DISTANCE);
    }

    if (value == "vehicle-reduction") {
        return boost::make_optional(ThirdStageStrategy::VEHICLE_REDUCTION);
    }

    if (value == "delay-riskiness-reduction") {
        return boost::make_optional(ThirdStageStrategy::DELAY_RISKINESS_REDUCTION);
    }

    if (value == "delay-probability-reduction") {
        return boost::make_optional(ThirdStageStrategy::DELAY_PROBABILITY_REDUCTION);
    }

    return boost::none;
}

rows::ThirdStageStrategy rows::GetAlias(ThirdStageStrategy strategy) {
    if (strategy == ThirdStageStrategy::DEFAULT) {
        return ThirdStageStrategy::DISTANCE;
    }

    return strategy;
}

std::ostream &rows::operator<<(std::ostream &out, rows::ThirdStageStrategy value) {
    switch (value) {
        case ThirdStageStrategy::DEFAULT:
            out << "DEFAULT";
            break;
        case ThirdStageStrategy::NONE:
            out << "NONE";
            break;

        case ThirdStageStrategy::DISTANCE:
            out << "DISTANCE";
            break;
        case ThirdStageStrategy::VEHICLE_REDUCTION:
            out << "VEHICLE_REDUCTION";
            break;
        case ThirdStageStrategy::DELAY_RISKINESS_REDUCTION:
            out << "DELAY_RISKINESS_REDUCTION";
            break;
        case ThirdStageStrategy::DELAY_PROBABILITY_REDUCTION:
            out << "DELAY_PROBABILITY_REDUCTION";
            break;
        default:
            LOG(FATAL) << "Translation of value " << static_cast<int>(value) << " to string is not implemented";
    }
    return out;
}