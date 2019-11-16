#include <util/routing.h>
#include <ortools/constraint_solver/routing_parameters.h>
#include <absl/time/time.h>
#include <ortools/base/protoutil.h>

#include <utility>

#include "util/input.h"
#include "three_step_worker.h"
#include "third_step_solver.h"
#include "multi_carer_solver.h"
#include "third_step_reduction_solver.h"

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

const std::vector<std::pair<rows::Carer, rows::Diary> > &
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

int64 GetMaxDistance(rows::SolverWrapper &solver, const std::vector<std::vector<operations_research::RoutingNodeIndex> > &solution) {
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

    std::unique_ptr<rows::SecondStepSolver> second_stage_wrapper = std::make_unique<rows::SecondStepSolver>(*problem_data_,
                                                                                                            second_stage_search_params,
                                                                                                            visit_time_window_,
                                                                                                            break_time_window_,
                                                                                                            begin_end_shift_time_extension_,
                                                                                                            opt_time_limit_);

    if (second_stage_wrapper->vehicles() == 0) {
        LOG(FATAL) << "No carers available";
        printer_->operator<<(TracingEvent(TracingEventType::Finished, "All"));
        SetReturnCode(1);
        return;
    }

    std::unique_ptr<operations_research::RoutingIndexManager> second_stage_index_manager
            = std::make_unique<operations_research::RoutingIndexManager>(second_stage_wrapper->nodes(),
                                                                         second_stage_wrapper->vehicles(),
                                                                         rows::RealProblemData::DEPOT);

    std::vector<std::vector<int64>> second_step_initial_routes{static_cast<std::size_t>(second_stage_wrapper->vehicles())};
    if (first_stage_strategy_ != FirstStageStrategy::NONE && has_multiple_carer_visits) {
        LOG(INFO) << "Solving the first stage using " << GetAlias(first_stage_strategy_) << " strategy";

        second_step_initial_routes = SolveFirstStage(*second_stage_wrapper, *second_stage_index_manager);

        auto all_routes_empty = true;
        for (const auto &route: second_step_initial_routes) {
            if (!route.empty()) {
                all_routes_empty = false;
                break;
            }
        }

        LOG_IF(WARNING, all_routes_empty) << "First stage did not produce any routes";
    }

    const auto second_stage_routes = SolveSecondStage(second_step_initial_routes,
                                                      *second_stage_wrapper,
                                                      *second_stage_index_manager,
                                                      second_stage_search_params);

    if (third_stage_strategy_ != ThirdStageStrategy::NONE) {
        LOG(INFO) << "Solving the third stage using " << GetAlias(third_stage_strategy_) << " strategy";
        SolveThirdStage(second_stage_routes, *second_stage_wrapper);
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
          data_factory_{data_factory} {}

std::vector<rows::ThreeStepSchedulingWorker::CarerTeam> rows::ThreeStepSchedulingWorker::GetCarerTeams(const rows::Problem &problem) {
    std::vector<std::pair<rows::Carer, rows::Diary> > carer_diaries;
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
        boost::optional<std::pair<rows::Carer, rows::Diary> > best_match = boost::none;
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
                                           std::string output_file,
                                           boost::posix_time::time_duration visit_time_window,
                                           boost::posix_time::time_duration break_time_window,
                                           boost::posix_time::time_duration begin_end_shift_time_extension,
                                           boost::posix_time::time_duration pre_opt_time_limit,
                                           boost::posix_time::time_duration opt_time_limit,
                                           boost::posix_time::time_duration post_opt_time_limit,
                                           double cost_normalization_factor) {
    problem_data_ = problem_data;
    output_file_ = std::move(output_file);
    visit_time_window_ = std::move(visit_time_window);
    break_time_window_ = std::move(break_time_window);
    begin_end_shift_time_extension_ = std::move(begin_end_shift_time_extension);
    pre_opt_time_limit_ = std::move(pre_opt_time_limit);
    opt_time_limit_ = std::move(opt_time_limit);
    post_opt_time_limit_ = std::move(post_opt_time_limit);
    cost_normalization_factor_ = cost_normalization_factor;
    return true;
}

std::unique_ptr<rows::SolverWrapper> rows::ThreeStepSchedulingWorker::CreateThirdStageSolver(
        const operations_research::RoutingSearchParameters &search_params,
        int64 last_dropped_visit_penalty,
        std::size_t max_dropped_visits_count) {
    CHECK_NE(third_stage_strategy_, ThirdStageStrategy::NONE);

    if (third_stage_strategy_ == ThirdStageStrategy::DEFAULT || third_stage_strategy_ == ThirdStageStrategy::DISTANCE) {
        return std::make_unique<rows::ThirdStepSolver>(*problem_data_,
                                                       search_params,
                                                       visit_time_window_,
                                                       break_time_window_,
                                                       begin_end_shift_time_extension_,
                                                       post_opt_time_limit_,
                                                       last_dropped_visit_penalty,
                                                       max_dropped_visits_count,
                                                       false);
    } else {
        CHECK_EQ(third_stage_strategy_, ThirdStageStrategy::VEHICLE_REDUCTION);
        return std::make_unique<rows::ThirdStepReductionSolver>(*problem_data_,
                                                                search_params,
                                                                visit_time_window_,
                                                                break_time_window_,
                                                                begin_end_shift_time_extension_,
                                                                post_opt_time_limit_,
                                                                last_dropped_visit_penalty,
                                                                max_dropped_visits_count);
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
//    parameters.mutable_local_search_operators()->set_use_tsp_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
//    parameters.mutable_local_search_operators()->set_use_full_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);

// we are fishing for optimal solutions in scenarios in which all orders are performed
    parameters.mutable_local_search_operators()->set_use_cross_exchange(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.mutable_local_search_operators()->set_use_relocate_neighbors(operations_research::OptionalBoolean::BOOL_TRUE);
    parameters.set_local_search_metaheuristic(operations_research::LocalSearchMetaheuristic_Value_GUIDED_LOCAL_SEARCH);

// not worth as we don't usually deal with unperformed visits
//  parameters.mutable_local_search_operators()->set_use_extended_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
//  parameters.mutable_local_search_operators()->set_use_relocate_and_make_active(operations_research::OptionalBoolean::BOOL_TRUE);

    return parameters;
}

std::vector<std::vector<int64>> rows::ThreeStepSchedulingWorker::SolveFirstStage(const rows::SolverWrapper &second_step_wrapper,
                                                                                 const operations_research::RoutingIndexManager &second_step_index_manager) {
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
        std::unique_ptr<rows::SolverWrapper> first_stage_wrapper
                = std::make_unique<rows::SingleStepSolver>(*sub_problem_data,
                                                           search_params,
                                                           visit_time_window_,
                        // break time window is 0 for teams, because their breaks have to be synchronized
                                                           boost::posix_time::seconds(0),
                                                           boost::posix_time::not_a_date_time,
                                                           pre_opt_time_limit_);
        std::unique_ptr<operations_research::RoutingIndexManager> first_step_index_manager
                = std::make_unique<operations_research::RoutingIndexManager>(first_stage_wrapper->nodes(),
                                                                             first_stage_wrapper->vehicles(),
                                                                             rows::RealProblemData::DEPOT);
        std::unique_ptr<operations_research::RoutingModel> first_step_model
                = std::make_unique<operations_research::RoutingModel>(*first_step_index_manager);
        first_stage_wrapper->ConfigureModel(*first_step_index_manager, *first_step_model, printer_, CancelToken(), cost_normalization_factor_);

        printer_->operator<<(TracingEvent(TracingEventType::Started, "Stage1"));
//        first_step_model->solver()->set_fail_intercept(&FailureInterceptor);
        first_step_assignment = first_step_model->SolveWithParameters(search_params);
        printer_->operator<<(TracingEvent(TracingEventType::Finished, "Stage1"));

        if (first_step_assignment == nullptr) {
            throw util::ApplicationError("No first stage solution found.", util::ErrorCode::ERROR);
        }

        operations_research::Assignment first_validation_copy{first_step_assignment};
        const auto is_first_solution_correct = first_step_model->solver()->CheckAssignment(&first_validation_copy);
        DCHECK(is_first_solution_correct);

        std::vector<std::vector<int64>> first_step_solution;
        first_step_model->AssignmentToRoutes(*first_step_assignment, &first_step_solution);

        auto time_dim = first_step_model->GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
        auto route_number = 0;
        for (const auto &route : first_step_solution) {
            const auto &team_carer = first_stage_wrapper->Carer(route_number);
            const auto team_carer_find_it = teams.find(team_carer);
            DCHECK(team_carer_find_it != std::end(teams));

            for (const auto node_index : route) {
                const auto &visit = first_stage_wrapper->NodeToVisit(first_step_index_manager->IndexToNode(node_index));

                std::vector<int> vehicle_numbers;
                boost::posix_time::ptime visit_start_time{
                        team_carer_find_it->second.Diary().date(),
                        boost::posix_time::seconds(
                                first_step_assignment->Min(time_dim->CumulVar(node_index)))
                };

                for (const auto &carer : team_carer_find_it->second.AvailableMembers(visit_start_time,
                                                                                     first_stage_wrapper->GetAdjustment())) {
                    vehicle_numbers.push_back(second_step_wrapper.Vehicle(carer));
                }

                DCHECK_EQ(vehicle_numbers.size(), 2);
                DCHECK_NE(vehicle_numbers[0], vehicle_numbers[1]);

                const auto &visit_nodes = second_step_wrapper.GetNodes(visit);
                std::vector<int64> visit_indices_to_use = second_step_index_manager.NodesToIndices(visit_nodes);
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
        std::unique_ptr<rows::MultiCarerSolver> multi_carer_wrapper
                = std::make_unique<rows::MultiCarerSolver>(*sub_problem_data,
                                                           internal_search_params,
                                                           visit_time_window_,
                                                           break_time_window_,
                                                           begin_end_shift_time_extension_,
                                                           pre_opt_time_limit_);
        std::unique_ptr<operations_research::RoutingIndexManager> multi_carer_index_manager
                = std::make_unique<operations_research::RoutingIndexManager>(multi_carer_wrapper->nodes(),
                                                                             multi_carer_wrapper->vehicles(),
                                                                             rows::RealProblemData::DEPOT);
        std::unique_ptr<operations_research::RoutingModel> multi_carer_model
                = std::make_unique<operations_research::RoutingModel>(*multi_carer_index_manager);
        multi_carer_wrapper->ConfigureModel(*multi_carer_index_manager, *multi_carer_model, printer_, CancelToken(), cost_normalization_factor_);
        auto result = multi_carer_model->SolveWithParameters(internal_search_params);
        operations_research::Assignment const *multi_carer_assignment = multi_carer_wrapper->GetBestSolution();
        if (multi_carer_assignment == nullptr) {
            multi_carer_assignment = result;
        }

        if (multi_carer_assignment == nullptr) {
            throw util::ApplicationError("No first stage solution found.", util::ErrorCode::ERROR);
        }

        std::vector<std::vector<int64>> multi_carer_solution;
        multi_carer_model->AssignmentToRoutes(*multi_carer_assignment, &multi_carer_solution);

        auto time_dim = multi_carer_model->GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
        auto route_number = 0;
        std::unordered_set<int64> used_visit_indices;
        for (const auto &route : multi_carer_solution) {
            const auto &carer = multi_carer_wrapper->Carer(route_number);

            for (const auto node_index : route) {
                const auto &visit = multi_carer_wrapper->NodeToVisit(multi_carer_index_manager->IndexToNode(node_index));

                const auto &visit_nodes = second_step_wrapper.GetNodes(visit);
                std::vector<int64> visit_indices = second_step_index_manager.NodesToIndices(visit_nodes);
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

                const auto multi_carer_visit_nodes = multi_carer_wrapper->GetNodes(visit);
                const auto multi_carer_visit_indices = multi_carer_index_manager->NodesToIndices(multi_carer_visit_nodes);
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

std::vector<std::vector<int64>> rows::ThreeStepSchedulingWorker::SolveSecondStage(const std::vector<std::vector<int64> > &second_stage_initial_routes,
                                                                                  rows::SecondStepSolver &second_stage_solver,
                                                                                  const operations_research::RoutingIndexManager &second_stage_index_manager,
                                                                                  const operations_research::RoutingSearchParameters &search_params) {

    auto second_stage_model = std::make_unique<operations_research::RoutingModel>(second_stage_index_manager);
//    second_stage_model->solver()->set_fail_intercept(&FailureInterceptor);
    second_stage_solver.ConfigureModel(second_stage_index_manager, *second_stage_model, printer_, CancelToken(), cost_normalization_factor_);

    std::stringstream penalty_msg;
    penalty_msg << "MissedVisitPenalty: " << second_stage_solver.GetDroppedVisitPenalty();
    printer_->operator<<(TracingEvent(TracingEventType::Unknown, penalty_msg.str()));

    const auto second_stage_initial_assignment = second_stage_model->ReadAssignmentFromRoutes(second_stage_initial_routes, true);

    printer_->operator<<(TracingEvent(TracingEventType::Started, "Stage2"));
    auto second_stage_assignment = second_stage_model->SolveFromAssignmentWithParameters(second_stage_initial_assignment, search_params);
    printer_->operator<<(TracingEvent(TracingEventType::Finished, "Stage2"));

    if (second_stage_assignment == nullptr) {
        throw util::ApplicationError("No second stage solution found.", util::ErrorCode::ERROR);
    }

    for (int vehicle = 0; vehicle < second_stage_model->vehicles(); ++vehicle) {
        const auto validation_result = solution_validator_.ValidateFull(vehicle,
                                                                        *second_stage_assignment,
                                                                        second_stage_index_manager,
                                                                        *second_stage_model,
                                                                        second_stage_solver);
        CHECK(validation_result.error() == nullptr);
    }

    boost::filesystem::path output_path{output_file_};
    std::string second_stage_output_file{"second_stage_"};
    second_stage_output_file += output_path.filename().string();
    boost::filesystem::path second_stage_output = boost::filesystem::absolute(second_stage_output_file, output_path.parent_path());

    solution_writer_.Write(second_stage_output,
                           second_stage_solver,
                           second_stage_index_manager,
                           *second_stage_model, *second_stage_assignment, boost::none);

    return second_stage_solver.solution_repository()->GetSolution();
}

void rows::ThreeStepSchedulingWorker::SolveThirdStage(const std::vector<std::vector<int64> > &second_stage_routes,
                                                      rows::SecondStepSolver &second_stage_solver) {
    std::unique_ptr<operations_research::RoutingIndexManager> third_stage_index_manager
            = std::make_unique<operations_research::RoutingIndexManager>(second_stage_solver.nodes(),
                                                                         second_stage_solver.vehicles(),
                                                                         rows::RealProblemData::DEPOT);

    std::unique_ptr<operations_research::RoutingModel> third_stage_model
            = std::make_unique<operations_research::RoutingModel>(*third_stage_index_manager);


    const auto max_dropped_visits_count = third_stage_model->nodes()
                                          - util::GetVisitedNodes(second_stage_routes, third_stage_model->GetDepot()).size() - 1;


    const auto third_search_params = CreateThirdStageRoutingSearchParameters();
    const auto third_stage_penalty = second_stage_solver.GetDroppedVisitPenalty();
    std::unique_ptr<rows::SolverWrapper> third_step_solver = CreateThirdStageSolver(third_search_params,
                                                                                    third_stage_penalty,
                                                                                    max_dropped_visits_count);

    third_step_solver->ConfigureModel(*third_stage_index_manager, *third_stage_model, printer_, CancelToken(), cost_normalization_factor_);

//    third_stage_model->solver()->set_fail_intercept(FailureInterceptor);
    const auto third_stage_preassignment = third_stage_model->ReadAssignmentFromRoutes(second_stage_routes, true);
    DCHECK(third_stage_preassignment);

    printer_->operator<<(TracingEvent(TracingEventType::Started, "Stage3"));
    operations_research::Assignment const *third_stage_assignment
            = third_stage_model->SolveFromAssignmentWithParameters(third_stage_preassignment, third_search_params);
    printer_->operator<<(TracingEvent(TracingEventType::Finished, "Stage3"));

    if (third_stage_assignment == nullptr) {
        throw util::ApplicationError("No third stage solution found.", util::ErrorCode::ERROR);
    }

    operations_research::Assignment third_validation_copy{third_stage_assignment};
    const auto is_third_solution_correct = third_stage_assignment->solver()->CheckAssignment(&third_validation_copy);
    DCHECK(is_third_solution_correct);

    std::map<int, std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> > > activities;
    for (int vehicle = 0; vehicle < third_stage_model->vehicles(); ++vehicle) {
        const auto validation_result = solution_validator_.ValidateFull(vehicle,
                                                                        *third_stage_assignment,
                                                                        *third_stage_index_manager,
                                                                        *third_stage_model,
                                                                        *third_step_solver);
        CHECK(validation_result.error() == nullptr);

        activities[vehicle] = validation_result.activities();
    }

    solution_writer_.Write(output_file_, *third_step_solver, *third_stage_index_manager, *third_stage_model,
                           *third_stage_assignment,
                           boost::make_optional(activities));

}

std::vector<rows::RouteValidatorBase::Metrics>
rows::ThreeStepSchedulingWorker::GetVehicleMetrics(const std::vector<std::vector<int64> > &routes, const rows::SolverWrapper &second_stage_wrapper) {
    const auto search_params = operations_research::DefaultRoutingSearchParameters();

    std::unique_ptr<rows::SecondStepSolver> intermediate_wrapper
            = std::make_unique<rows::SecondStepSolver>(*problem_data_,
                                                       search_params,
                                                       visit_time_window_,
                                                       break_time_window_,
                                                       begin_end_shift_time_extension_,
                                                       boost::date_time::not_a_date_time);
    std::unique_ptr<operations_research::RoutingIndexManager> intermediate_index_manager
            = std::make_unique<operations_research::RoutingIndexManager>(second_stage_wrapper.nodes(),
                                                                         second_stage_wrapper.vehicles(),
                                                                         rows::RealProblemData::DEPOT);
    std::unique_ptr<operations_research::RoutingModel> intermediate_model
            = std::make_unique<operations_research::RoutingModel>(*intermediate_index_manager);
    intermediate_wrapper->ConfigureModel(*intermediate_index_manager, *intermediate_model, printer_, CancelToken(), cost_normalization_factor_);

// useful for debugging
//    const auto warm_start_solution = util::LoadSolution(
//            "/home/pmateusz/dev/cordia/simulations/current_review_simulations/benchmark/25/solutions/solution_20171001_v25m0c3_mip.gexf",
//            problem_, visit_time_window_);
//
//    const auto routes = intermediate_wrapper->GetRoutes(warm_start_solution, *intermediate_index_manager, *intermediate_model);

    auto assignment_to_use = intermediate_model->ReadAssignmentFromRoutes(routes, true);
    CHECK(assignment_to_use);
    std::vector<rows::RouteValidatorBase::Metrics> vehicle_metrics;
    for (int vehicle = 0; vehicle < intermediate_model->vehicles(); ++vehicle) {
        const auto validation_result = solution_validator_.ValidateFull(vehicle,
                                                                        *assignment_to_use,
                                                                        *intermediate_index_manager,
                                                                        *intermediate_model,
                                                                        *intermediate_wrapper);
        CHECK(validation_result.error() == nullptr);
        vehicle_metrics.emplace_back(validation_result.metrics());
    }
    intermediate_model.reset();
    intermediate_wrapper.reset();

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
        default:
            LOG(FATAL) << "Translation of value " << static_cast<int>(value) << " to string is not implemented";
    }
    return out;
}