#include <util/routing.h>
#include "three_step_worker.h"
#include "third_step_solver.h"
#include "gexf_writer.h"

rows::ThreeStepSchedulingWorker::CarerTeam::CarerTeam(std::pair<rows::Carer, rows::Diary> member)
        : diary_{member.second} {
    members_.emplace_back(std::move(member));
}

void rows::ThreeStepSchedulingWorker::CarerTeam::Add(std::pair<rows::Carer, rows::Diary> member) {
    DCHECK(std::find_if(std::cbegin(members_), std::cend(members_),
                        [&member](const std::pair<rows::Carer, rows::Diary> &local_member) -> bool {
                            return local_member.first == member.first;
                        }) == std::cend(members_));

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

int64 GetMaxDistance(rows::SolverWrapper &solver,
                     const std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > &solution) {
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
    std::vector<std::pair<rows::Carer, std::vector<rows::Diary> > > team_carers;
    std::unordered_map<rows::Carer, CarerTeam> teams;
    int id = 0;
    for (auto &team : GetCarerTeams(problem_)) {
        rows::Carer carer{(boost::format("team-%1%") % ++id).str(), rows::Transport::Foot};

        if (team.size() > 1) {
            team_carers.push_back({carer, {team.Diary()}});
        }

        teams.emplace(std::move(carer), team);
    }

    std::vector<rows::CalendarVisit> team_visits;
    for (const auto &visit: problem_.visits()) {
        if (visit.carer_count() > 1) {
            rows::CalendarVisit visit_copy{visit};
            visit_copy.carer_count(1);

            team_visits.emplace_back(std::move(visit_copy));
        }
    }

    // some visits in the test problem are duplicated
    rows::Problem sub_problem{team_visits, team_carers, problem_.service_users()};
    const auto search_params = rows::SolverWrapper::CreateSearchParameters();
    std::unique_ptr<rows::SolverWrapper> first_stage_wrapper
            = std::make_unique<rows::SingleStepSolver>(sub_problem,
                                                       routing_parameters_,
                                                       search_params,
                                                       visit_time_window_,
                    // break time window is 0 for teams, because their breaks have to be synchronized
                                                       boost::posix_time::minutes(0),
                                                       boost::posix_time::not_a_date_time,
                                                       pre_opt_time_limit_);
    std::unique_ptr<operations_research::RoutingModel> first_step_model
            = std::make_unique<operations_research::RoutingModel>(first_stage_wrapper->nodes(),
                                                                  first_stage_wrapper->vehicles(),
                                                                  rows::SolverWrapper::DEPOT);
    first_stage_wrapper->ConfigureModel(*first_step_model, printer_, CancelToken());
    operations_research::Assignment const *first_step_assignment = first_step_model->SolveWithParameters(search_params);

    if (first_step_assignment == nullptr) {
        throw util::ApplicationError("No first stage solution found.", util::ErrorCode::ERROR);
    }

    operations_research::Assignment first_validation_copy{first_step_assignment};
    const auto is_first_solution_correct = first_step_model->solver()->CheckAssignment(&first_validation_copy);
    DCHECK(is_first_solution_correct);

    std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > first_step_solution;
    first_step_model->AssignmentToRoutes(*first_step_assignment, &first_step_solution);

    // TODO: penalty extracted in a weird way
    auto dropped_visit_penalty = first_stage_wrapper->GetDroppedVisitPenalty(*first_step_model);
    std::unique_ptr<rows::SecondStepSolver> second_stage_wrapper
            = std::make_unique<rows::SecondStepSolver>(problem_,
                                                       routing_parameters_,
                                                       search_params,
                                                       visit_time_window_,
                                                       break_time_window_,
                                                       begin_end_shift_time_extension_,
                                                       opt_time_limit_,
                                                       dropped_visit_penalty);

    std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > second_step_locks{
            static_cast<std::size_t>(second_stage_wrapper->vehicles())};
    auto time_dim = first_step_model->GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
    auto route_number = 0;
    for (const auto &route : first_step_solution) {
        const auto &team_carer = first_stage_wrapper->Carer(route_number);
        const auto team_carer_find_it = teams.find(team_carer);
        DCHECK(team_carer_find_it != std::end(teams));

        for (const auto node : route) {
            const auto &visit = first_stage_wrapper->NodeToVisit(node);

            std::vector<int> vehicle_numbers;
            boost::posix_time::ptime visit_start_time{
                    team_carer_find_it->second.Diary().date(),
                    boost::posix_time::seconds(
                            first_step_assignment->Min(time_dim->CumulVar(first_step_model->NodeToIndex(node))))
            };

            for (const auto &carer : team_carer_find_it->second.AvailableMembers(visit_start_time,
                                                                                 first_stage_wrapper->GetAdjustment())) {
                vehicle_numbers.push_back(second_stage_wrapper->Vehicle(carer));
            }

            DCHECK_EQ(vehicle_numbers.size(), 2);
            DCHECK_NE(vehicle_numbers[0], vehicle_numbers[1]);

            const auto visit_nodes = second_stage_wrapper->GetNodes(visit);
            std::vector<operations_research::RoutingModel::NodeIndex> visit_nodes_to_use{
                    std::begin(visit_nodes),
                    std::end(visit_nodes)};
            DCHECK_EQ(visit_nodes_to_use.size(), vehicle_numbers.size());


            auto first_vehicle_to_use = vehicle_numbers[0];
            auto second_vehicle_to_use = vehicle_numbers[1];
            if (first_vehicle_to_use > second_vehicle_to_use) {
                std::swap(first_vehicle_to_use, second_vehicle_to_use);
            }

            auto first_visit_to_use = visit_nodes_to_use[0];
            auto second_visit_to_use = visit_nodes_to_use[1];
            if (first_visit_to_use > second_visit_to_use) {
                std::swap(first_visit_to_use, second_visit_to_use);
            }

            second_step_locks[first_vehicle_to_use].push_back(first_visit_to_use);
            second_step_locks[second_vehicle_to_use].push_back(second_visit_to_use);
        }

        ++route_number;
    }

    first_step_model.release();

    std::unique_ptr<operations_research::RoutingModel> second_stage_model
            = std::make_unique<operations_research::RoutingModel>(second_stage_wrapper->nodes(),
                                                                  second_stage_wrapper->vehicles(),
                                                                  rows::SolverWrapper::DEPOT);
    second_stage_wrapper->ConfigureModel(*second_stage_model, printer_, CancelToken());
    const auto computed_assignment = second_stage_model->ReadAssignmentFromRoutes(second_step_locks, true);
    DCHECK(computed_assignment);
    if (lock_partial_paths_) {
        const auto locks_applied = second_stage_model->ApplyLocksToAllVehicles(second_step_locks, false);
        DCHECK(locks_applied);
        DCHECK(second_stage_model->PreAssignment());
    }

    operations_research::Assignment const *second_stage_assignment
            = second_stage_model->SolveFromAssignmentWithParameters(computed_assignment, search_params);
    if (second_stage_assignment == nullptr) {
        throw util::ApplicationError("No second stage solution found.", util::ErrorCode::ERROR);
    }

    const auto routes = second_stage_wrapper->solution_repository()->GetSolution();
    second_stage_model.release();

    std::unique_ptr<operations_research::RoutingModel> third_stage_model
            = std::make_unique<operations_research::RoutingModel>(second_stage_wrapper->nodes(),
                                                                  second_stage_wrapper->vehicles(),
                                                                  rows::SolverWrapper::DEPOT);
    std::unique_ptr<rows::ThirdStepSolver> third_step_solver
            = std::make_unique<rows::ThirdStepSolver>(problem_,
                                                      routing_parameters_,
                                                      search_params,
                                                      visit_time_window_,
                                                      break_time_window_,
                                                      begin_end_shift_time_extension_,
                                                      post_opt_time_limit_,
                                                      dropped_visit_penalty,
                                                      util::GetVisitedNodes(routes, rows::SolverWrapper::DEPOT).size());

    third_step_solver->ConfigureModel(*third_stage_model, printer_, CancelToken());
    const auto third_stage_preassignment = third_stage_model->ReadAssignmentFromRoutes(routes, true);
    DCHECK(third_stage_preassignment);

    operations_research::Assignment const *third_stage_assignment
            = third_stage_model->SolveFromAssignmentWithParameters(third_stage_preassignment,
                                                                   search_params);
    if (third_stage_assignment == nullptr) {
        throw util::ApplicationError("No third stage solution found.", util::ErrorCode::ERROR);
    }

    operations_research::Assignment third_validation_copy{third_stage_assignment};
    const auto is_third_solution_correct = third_stage_assignment->solver()->CheckAssignment(&third_validation_copy);
    DCHECK(is_third_solution_correct);

    rows::GexfWriter solution_writer;
    solution_writer.Write(output_file_, *third_step_solver, *third_stage_model, *third_stage_assignment);

    SetReturnCode(0);
}

rows::ThreeStepSchedulingWorker::ThreeStepSchedulingWorker(std::shared_ptr<rows::Printer> printer) :
        printer_{std::move(printer)},
        lock_partial_paths_{false},
        pre_opt_time_limit_{boost::posix_time::not_a_date_time},
        opt_time_limit_{boost::posix_time::not_a_date_time},
        post_opt_time_limit_{boost::posix_time::not_a_date_time} {}

std::vector<rows::ThreeStepSchedulingWorker::CarerTeam>
rows::ThreeStepSchedulingWorker::GetCarerTeams(const rows::Problem &problem) {
    std::vector<std::pair<rows::Carer, rows::Diary> > carer_diaries;
    for (const auto &carer_diary_pair : problem_.carers()) {
        CHECK_EQ(carer_diary_pair.second.size(), 1);
        carer_diaries.emplace_back(carer_diary_pair.first, carer_diary_pair.second.front());
    }

    std::sort(std::begin(carer_diaries), std::end(carer_diaries),
              [](const std::pair<rows::Carer, rows::Diary> &left,
                 const std::pair<rows::Carer, rows::Diary> &right) -> bool {

                  return left.second.duration() >= right.second.duration();
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
        for (auto possible_match_it = std::next(carer_diary_it);
             possible_match_it != carer_diary_it_end;
             ++possible_match_it) {

            if (processed_carers.find(possible_match_it->first) != std::end(processed_carers)) {
                continue;
            }

            const auto match_diary = carer_diary_it->second.Intersect(possible_match_it->second);
            if (!best_match_diary || (best_match_diary->duration() < match_diary.duration())) {
                best_match = *possible_match_it;
                best_match_diary = match_diary;
            }
        }

        if (best_match && best_match_diary->duration() >= boost::posix_time::time_duration(2, 30, 0, 0)) {
            if (!processed_carers.insert(best_match->first).second) {
                throw util::ApplicationError(
                        (boost::format("Carer %1% cannot be a member of more than 1 team")
                         % best_match->first).str(),
                        util::ErrorCode::ERROR);
            }
            team.Add(std::move(*best_match));
            teams.emplace_back(std::move(team));
        }
    }

    return teams;
}

bool rows::ThreeStepSchedulingWorker::Init(rows::Problem problem,
                                           osrm::EngineConfig routing_config,
                                           std::string output_file,
                                           boost::posix_time::time_duration visit_time_window,
                                           boost::posix_time::time_duration break_time_window,
                                           boost::posix_time::time_duration begin_end_shift_time_extension,
                                           boost::posix_time::time_duration pre_opt_time_limit,
                                           boost::posix_time::time_duration opt_time_limit,
                                           boost::posix_time::time_duration post_opt_time_limit) {
    problem_ = std::move(problem);
    routing_parameters_ = std::move(routing_config);
    output_file_ = std::move(output_file);
    visit_time_window_ = std::move(visit_time_window);
    break_time_window_ = std::move(break_time_window);
    begin_end_shift_time_extension_ = std::move(begin_end_shift_time_extension);
    pre_opt_time_limit_ = std::move(pre_opt_time_limit);
    opt_time_limit_ = std::move(opt_time_limit);
    post_opt_time_limit_ = std::move(post_opt_time_limit);
    return true;
}