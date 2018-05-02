#include "two_step_worker.h"

rows::TwoStepSchedulingWorker::CarerTeam::CarerTeam(std::pair<rows::Carer, rows::Diary> member)
        : diary_{member.second} {
    members_.emplace_back(std::move(member));
}

void rows::TwoStepSchedulingWorker::CarerTeam::Add(std::pair<rows::Carer, rows::Diary> member) {
    DCHECK(std::find_if(std::cbegin(members_), std::cend(members_),
                        [&member](const std::pair<rows::Carer, rows::Diary> &local_member) -> bool {
                            return local_member.first == member.first;
                        }) == std::cend(members_));

    diary_ = diary_.Intersect(member.second);
    members_.emplace_back(std::move(member));
}

std::size_t rows::TwoStepSchedulingWorker::CarerTeam::size() const {
    return members_.size();
}

std::vector<rows::Carer> rows::TwoStepSchedulingWorker::CarerTeam::Members() const {
    std::vector<rows::Carer> result;
    for (const auto &member : members_) {
        result.push_back(member.first);
    }
    return result;
}

const std::vector<std::pair<rows::Carer, rows::Diary> > &rows::TwoStepSchedulingWorker::CarerTeam::FullMembers() const {
    return members_;
}

std::vector<rows::Carer>
rows::TwoStepSchedulingWorker::CarerTeam::AvailableMembers(const boost::posix_time::ptime date_time,
                                                           const boost::posix_time::time_duration &adjustment) const {
    std::vector<rows::Carer> result;
    for (const auto &member : members_) {
        if (member.second.IsAvailable(date_time, adjustment)) {
            result.push_back(member.first);
        }
    }
    return result;
}

const rows::Diary &rows::TwoStepSchedulingWorker::CarerTeam::Diary() const {
    return diary_;
}

void rows::TwoStepSchedulingWorker::Run() {
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

    LOG(INFO) << "Teams:";
    for (const auto &team : teams) {
        LOG(INFO) << "Team: " << team.first.sap_number() << " " << team.second.Diary().duration();
        for (const auto &event : team.second.Diary().events()) {
            LOG(INFO) << event;
        }
    }

    LOG(INFO) << "Visits:";
    std::vector<rows::CalendarVisit> team_visits;
    for (const auto &visit: problem_.visits()) {
        if (visit.carer_count() > 1) {
            rows::CalendarVisit visit_copy{visit};
            visit_copy.carer_count(1);

            LOG(INFO) << visit_copy.service_user() << " " << visit_copy.datetime();
            team_visits.emplace_back(std::move(visit_copy));
        }
    }

    // some visits in the test problem are duplicated
    rows::Problem sub_problem{team_visits, team_carers, problem_.service_users()};

    auto first_step_search_params = rows::SolverWrapper::CreateSearchParameters();
    first_step_search_params.set_time_limit_ms(20 * 1000);
    std::unique_ptr<rows::SolverWrapper> first_stage_wrapper
            = std::make_unique<rows::SingleStepSolver>(sub_problem,
                                                       routing_parameters_,
                                                       first_step_search_params,
                                                       boost::posix_time::minutes(0),
                                                       false);
    std::unique_ptr<operations_research::RoutingModel> first_step_model
            = std::make_unique<operations_research::RoutingModel>(first_stage_wrapper->nodes(),
                                                                  first_stage_wrapper->vehicles(),
                                                                  rows::SolverWrapper::DEPOT);
    first_stage_wrapper->ConfigureModel(*first_step_model, printer_, CancelToken());
    operations_research::Assignment const *first_step_assignment = first_step_model->SolveWithParameters(
            first_step_search_params);

    if (first_step_assignment == nullptr) {
        throw util::ApplicationError("No first stage solution found.", util::ErrorCode::ERROR);
    }

    operations_research::Assignment first_validation_copy{first_step_assignment};
    const auto is_first_solution_correct = first_step_model->solver()->CheckAssignment(&first_validation_copy);
    DCHECK(is_first_solution_correct);

    LOG(INFO) << "First step solved to completion";
    auto second_step_search_params = rows::SolverWrapper::CreateSearchParameters(); // operations_research::RoutingModel::DefaultSearchParameters();
    std::unique_ptr<rows::SolverWrapper> second_stage_wrapper
            = std::make_unique<rows::TwoStepSolver>(problem_, routing_parameters_, second_step_search_params);

    std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > first_step_solution;
    first_step_model->AssignmentToRoutes(*first_step_assignment, &first_step_solution);

    std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > second_step_locks{
            static_cast<std::size_t>(second_stage_wrapper->vehicles())};
    auto time_dim = first_step_model->GetMutableDimension(rows::SolverWrapper::TIME_DIMENSION);
    auto route_number = 0;
    for (const auto &route : first_step_solution) {
        const auto &team_carer = first_stage_wrapper->Carer(route_number);
        const auto team_carer_find_it = teams.find(team_carer);
        DCHECK(team_carer_find_it != std::end(teams));

        std::vector<std::string> nodes;
        std::vector<rows::CalendarVisit> visits;
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

            LOG(INFO) << boost::format("%1% %2% -> %3% %4%")
                         % first_vehicle_to_use
                         % second_vehicle_to_use
                         % first_visit_to_use
                         % second_visit_to_use;

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

    for (const auto &route : second_step_locks) {
        std::vector<std::string> raw_route;
        for (const auto node : route) {
            raw_route.push_back(std::to_string(node.value()));
        }

        LOG(INFO) << boost::algorithm::join(raw_route, " -> ");
    }

    const auto computed_assignment = second_stage_model->ReadAssignmentFromRoutes(second_step_locks, true);
    DCHECK(computed_assignment);

    if (lock_partial_paths_) {
        const auto locks_applied = second_stage_model->ApplyLocksToAllVehicles(second_step_locks, false);
        DCHECK(locks_applied);
        DCHECK(second_stage_model->PreAssignment());
    }

    operations_research::Assignment const *second_stage_assignment
            = second_stage_model->SolveFromAssignmentWithParameters(computed_assignment,
                                                                    second_step_search_params);

    if (second_stage_assignment == nullptr) {
        throw util::ApplicationError("No second stage solution found.", util::ErrorCode::ERROR);
    }

    auto third_step_routing_parameters = rows::SolverWrapper::CreateSearchParameters();

    operations_research::Assignment second_validation_copy{second_stage_assignment};
    const auto is_second_solution_correct = second_stage_model->solver()->CheckAssignment(
            &second_validation_copy);
    DCHECK(is_second_solution_correct);

    std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > third_stage_initial_routes;
    second_stage_model->AssignmentToRoutes(*second_stage_assignment, &third_stage_initial_routes);

    second_stage_model.release();

    std::unique_ptr<rows::SolverWrapper> third_stage_wrapper
            = std::make_unique<rows::TwoStepSolver>(problem_, routing_parameters_, second_step_search_params);

    std::unique_ptr<operations_research::RoutingModel> third_stage_model
            = std::make_unique<operations_research::RoutingModel>(third_stage_wrapper->nodes(),
                                                                  third_stage_wrapper->vehicles(),
                                                                  rows::SolverWrapper::DEPOT);

    third_stage_wrapper->ConfigureModel(*third_stage_model, printer_, CancelToken());

    ResetCancelToken();

    operations_research::Assignment const *initial_guess_assignment
            = third_stage_model->ReadAssignmentFromRoutes(third_stage_initial_routes, false);
    DCHECK(initial_guess_assignment);

    operations_research::Assignment const *third_stage_assignment
            = third_stage_model->SolveFromAssignmentWithParameters(initial_guess_assignment,
                                                                   third_step_routing_parameters);

    if (third_stage_assignment == nullptr) {
        throw util::ApplicationError("No third stage solution found.", util::ErrorCode::ERROR);
    }

    SetReturnCode(0);
}

rows::TwoStepSchedulingWorker::TwoStepSchedulingWorker(std::shared_ptr<rows::Printer> printer) :
        printer_{std::move(printer)},
        lock_partial_paths_{false} {}

std::vector<rows::TwoStepSchedulingWorker::CarerTeam>
rows::TwoStepSchedulingWorker::GetCarerTeams(const rows::Problem &problem) {
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

bool rows::TwoStepSchedulingWorker::Init(rows::Problem problem, osrm::EngineConfig routing_config) {
    problem_ = std::move(problem);
    routing_parameters_ = std::move(routing_config);
    return true;
}
