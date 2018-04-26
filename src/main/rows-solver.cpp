#include <string>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include <memory>
#include <thread>
#include <future>
#include <iostream>
#include <regex>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/range/irange.hpp>
#include <boost/algorithm/string/join.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <nlohmann/json.hpp>

#include <ortools/constraint_solver/routing.h>
#include <ortools/constraint_solver/routing_flags.h>

#include <osrm/engine/engine_config.hpp>
#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <osrm/osrm.hpp>

#include <libgexf/libgexf.h>

#include "util/aplication_error.h"
#include "util/logging.h"
#include "util/validation.h"
#include "location.h"
#include "location_container.h"
#include "carer.h"
#include "event.h"
#include "diary.h"
#include "calendar_visit.h"
#include "solution.h"
#include "problem.h"
#include "printer.h"
#include "solver_wrapper.h"
#include "gexf_writer.h"
#include "single_step_solver.h"
#include "instant_transfer_solver.h"
#include "two_step_solver.h"


static const int STATUS_OK = 1;
static const int NOT_STARTED = -1;

DEFINE_string(problem, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists);

DEFINE_string(solution, "", "a file path to the solution file for warm start");
DEFINE_validator(solution, &util::file::IsNullOrExists);

DEFINE_string(maps, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(maps, &util::file::Exists);

static const std::string JSON_FORMAT{"json"};
static const std::string TEXT_FORMAT{"txt"};

bool ValidateConsoleFormat(const char *flagname, const std::string &value) {
    std::string value_to_use{value};
    util::string::Strip(value_to_use);
    util::string::ToLower(value_to_use);
    return value_to_use == JSON_FORMAT || value_to_use == TEXT_FORMAT;
}

DEFINE_string(console_format, "txt", "output format. Available options: txt or json");
DEFINE_validator(console_format, &ValidateConsoleFormat);

DEFINE_string(output, "solution.gexf", "a file path to save the solution");
DEFINE_validator(output, &util::file::IsNullOrNotExists);

DEFINE_string(time_limit, "", "total time dedicated for computation");
DEFINE_validator(time_limit, &util::time_duration::IsNullOrPositive);

static const auto DEFAULT_SOLUTION_LIMIT = std::numeric_limits<int64>::max();
DEFINE_int64(solutions_limit,
             DEFAULT_SOLUTION_LIMIT,
             "total number of solutions considered in the computation");
DEFINE_validator(solutions_limit, &util::numeric::IsPositive);

DEFINE_string(scheduling_date,
              "",
              "day to compute schedule for. By default it is the day of the earliest requested visit in the problem");
DEFINE_validator(scheduling_date, &util::date::IsNullOrPositive);

void ParseArgs(int argc, char **argv) {
    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling\n"
                            "Example: rows-main"
                            " --problem=problem.json"
                            " --maps=./data/scotland-latest.osrm"
                            " --solution=past_solution.json"
                            " --scheduling-date=2017-01-13"
                            " --output=solution.gexf"
                            " --time-limit=00:30:00"
                            " --solutions-limit=1024");

    FLAGS_output = util::file::GenerateNewFilePath("solution.gexf");

    static const auto REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\n"
                             "problem: %1%\n"
                             "maps: %2%\n"
                             "solution: %3%\n"
                             "scheduling-date: %4%\n"
                             "output: %5%\n"
                             "time-limit: %6%\n"
                             "solutions-limit: %7%")
               % FLAGS_problem
               % FLAGS_maps
               % FLAGS_solution
               % (FLAGS_scheduling_date.empty() ? "not set" : FLAGS_scheduling_date)
               % FLAGS_output
               % (FLAGS_time_limit.empty() ? "no" : FLAGS_time_limit)
               % FLAGS_solutions_limit;
}

std::string GetModelStatus(int status) {
    switch (status) {
        case operations_research::RoutingModel::Status::ROUTING_FAIL:
            return "ROUTING_FAIL";
        case operations_research::RoutingModel::Status::ROUTING_FAIL_TIMEOUT:
            return "ROUTING_FAIL_TIMEOUT";
        case operations_research::RoutingModel::Status::ROUTING_INVALID:
            return "ROUTING_INVALID";
        case operations_research::RoutingModel::Status::ROUTING_NOT_SOLVED:
            return "ROUTING_NOT_SOLVED";
        case operations_research::RoutingModel::Status::ROUTING_SUCCESS:
            return "ROUTING_SUCCESS";
    }
}

rows::Problem LoadReducedProblem(std::shared_ptr<rows::Printer> printer) {
    boost::filesystem::path problem_file(boost::filesystem::canonical(FLAGS_problem));
    std::ifstream problem_stream;
    problem_stream.open(problem_file.c_str());
    if (!problem_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     util::ErrorCode::ERROR);
    }

    nlohmann::json problem_json;
    try {
        problem_stream >> problem_json;
    } catch (...) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     boost::current_exception_diagnostic_information(),
                                     util::ErrorCode::ERROR);
    }

    rows::Problem problem;
    try {
        rows::Problem::JsonLoader json_loader;
        problem = json_loader.Load(problem_json);
    } catch (const std::domain_error &ex) {
        throw util::ApplicationError(
                (boost::format("Failed to parse the file %1% due to error: '%2%'") % problem_file %
                 ex.what()).str(),
                util::ErrorCode::ERROR);
    }


    const std::pair<boost::posix_time::ptime, boost::posix_time::ptime> timespan_pair = problem.Timespan();
    const auto begin_date = timespan_pair.first.date();
    const auto end_date = timespan_pair.second.date();

    if (FLAGS_scheduling_date.empty()) {
        if (begin_date < end_date) {
            printer->operator<<(
                    (boost::format("Problem contains records from several days."
                                   " The computed solution will be reduced to a single day: '%1%'")
                     % begin_date).str());

            return problem.Trim(timespan_pair.first, boost::posix_time::hours(24));
        }

        return problem;
    }

    const boost::posix_time::ptime scheduling_time{boost::gregorian::from_simple_string(FLAGS_scheduling_date)};
    const auto scheduling_date = scheduling_time.date();
    if (begin_date == end_date && begin_date == scheduling_date) {
        return problem;
    } else if (begin_date <= scheduling_date && scheduling_date <= end_date) {
        return problem.Trim(scheduling_time, boost::posix_time::hours(24));
    } else {
        throw util::ApplicationError(
                (boost::format("Scheduling day '%1%' does not fin into the interval ['%2%','%3%']")
                 % scheduling_date
                 % timespan_pair.first
                 % timespan_pair.second).str(),
                util::ErrorCode::ERROR);
    }
}

osrm::EngineConfig CreateEngineConfig(const std::string &maps_file) {
    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig(maps_file);
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    if (!config.IsValid()) {
        throw util::ApplicationError("Invalid Open Street Map engine configuration", util::ErrorCode::ERROR);
    }

    return config;
}

class SchedulingWorker {
public:
    SchedulingWorker()
            : return_code_{NOT_STARTED},
              cancel_token_{false},
              worker_{} {}

    virtual ~SchedulingWorker() = default;

    virtual void Run() = 0;

    void Start() {
        worker_ = std::thread(&SchedulingWorker::Run, this);
    }

    void Join() {
        worker_.join();
    }

    void Cancel() {
        VLOG(1) << "Cancellation requested";
        cancel_token_ = true;
    }

    int ReturnCode() const {
        return return_code_;
    }

protected:
    const std::atomic<bool> &CancelToken() const {
        return cancel_token_;
    }

    void ResetCancelToken() {
        cancel_token_ = false;
    }

    void SetReturnCode(int return_code) {
        return_code_ = return_code;
    }

private:
    std::atomic<int> return_code_;
    std::atomic<bool> cancel_token_;
    std::thread worker_;
};

class SingleStepSchedulingWorker : public SchedulingWorker {
public:
    explicit SingleStepSchedulingWorker(std::shared_ptr<rows::Printer> printer) :
            printer_{std::move(printer)},
            initial_assignment_{nullptr} {}

    ~SingleStepSchedulingWorker() override {
        if (model_) {
            model_.reset();
        }

        if (solver_) {
            solver_.reset();
        }

        initial_assignment_ = nullptr;
    }

    bool Init(const std::string &problem_file,
              const std::string &map_file,
              const std::string &past_solution_file,
              const std::string &time_limit,
              int64 solution_limit,
              std::string output_file) {
        try {
            auto problem_to_use = LoadReducedProblem(printer_);

            boost::optional<rows::Solution> solution;
            if (!past_solution_file.empty()) {
                solution = LoadSolution(past_solution_file, problem_to_use);
                solution.get().UpdateVisitProperties(problem_to_use.visits());
                problem_to_use.RemoveCancelled(solution.get().visits());
            }

            auto engine_config = CreateEngineConfig(map_file);
            auto search_parameters = rows::SolverWrapper::CreateSearchParameters();

            if (solution_limit != DEFAULT_SOLUTION_LIMIT) {
                search_parameters.set_solution_limit(solution_limit);
            }

            if (!time_limit.empty()) {
                const auto duration_limit = boost::posix_time::duration_from_string(time_limit);
                search_parameters.set_time_limit_ms(duration_limit.total_milliseconds());
            }

            solver_ = std::make_unique<rows::SingleStepSolver>(problem_to_use, engine_config, search_parameters);
            model_ = std::make_unique<operations_research::RoutingModel>(solver_->nodes(),
                                                                         solver_->vehicles(),
                                                                         rows::SolverWrapper::DEPOT);

            solver_->ConfigureModel(*model_, printer_, CancelToken());
            VLOG(1) << "Completed routing model configuration with status: " << GetModelStatus(model_->status());
            if (solution) {
                VLOG(1) << "Starting with a solution.";
                VLOG(1) << solution.get().DebugStatus(*solver_, *model_);
                const auto solution_to_use = solver_->ResolveValidationErrors(solution.get(), *model_);
                VLOG(1) << solution_to_use.DebugStatus(*solver_, *model_);

                if (VLOG_IS_ON(2)) {
                    for (const auto &visit : solution_to_use.visits()) {
                        if (visit.carer().is_initialized()) {
                            VLOG(2) << visit;
                        }
                    }
                }

                const auto routes = solver_->GetRoutes(solution_to_use, *model_);
                initial_assignment_ = model_->ReadAssignmentFromRoutes(routes, false);
                if (initial_assignment_ == nullptr || !model_->solver()->CheckAssignment(initial_assignment_)) {
                    throw util::ApplicationError("Solution for warm start is not valid.", util::ErrorCode::ERROR);
                }
            }

            output_file_ = std::move(output_file);
            return true;
        } catch (util::ApplicationError &ex) {
            LOG(ERROR) << ex.msg() << std::endl << ex.diagnostic_info();
            SetReturnCode(util::to_exit_code(ex.error_code()));
            return false;
        }
    }

private:
    rows::Solution LoadSolution(const std::string &solution_path, const rows::Problem &problem) {
        boost::filesystem::path solution_file(boost::filesystem::canonical(FLAGS_solution));
        std::ifstream solution_stream;
        solution_stream.open(solution_file.c_str());
        if (!solution_stream.is_open()) {
            throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                         util::ErrorCode::ERROR);
        }

        rows::Solution original_solution;
        const std::string file_extension{solution_file.extension().string()};
        if (file_extension == ".json") {
            nlohmann::json solution_json;
            try {
                solution_stream >> solution_json;
            } catch (...) {
                throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                             boost::current_exception_diagnostic_information(),
                                             util::ErrorCode::ERROR);
            }


            try {
                rows::Solution::JsonLoader json_loader;
                original_solution = json_loader.Load(solution_json);
            } catch (const std::domain_error &ex) {
                throw util::ApplicationError(
                        (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % solution_file %
                         ex.what()).str(),
                        util::ErrorCode::ERROR);
            }
        } else if (file_extension == ".gexf") {
            rows::Solution::XmlLoader xml_loader;
            original_solution = xml_loader.Load(solution_file.string());
        } else {
            throw util::ApplicationError(
                    (boost::format("Unknown file format: '%1%'. Use 'json' or 'gexf' format instead.")
                     % file_extension).str(), util::ErrorCode::ERROR);
        }

        const auto time_span = problem.Timespan();
        return original_solution.Trim(time_span.first, time_span.second - time_span.first);
    }

    void Run() override {
        try {
            if (initial_assignment_ == nullptr) {
                VLOG(1) << "Search started without a solution";
            } else {
                VLOG(1) << "Search started with a solution";
            }

            operations_research::Assignment const *assignment = model_->SolveFromAssignmentWithParameters(
                    initial_assignment_, solver_->parameters());

            VLOG(1) << "Search completed"
                    << "\nLocal search profile: " << model_->solver()->LocalSearchProfile()
                    << "\nDebug string: " << model_->solver()->DebugString()
                    << "\nModel status: " << GetModelStatus(model_->status());

            if (assignment == nullptr) {
                throw util::ApplicationError("No solution found.", util::ErrorCode::ERROR);
            }

            operations_research::Assignment validation_copy{assignment};
            const auto is_solution_correct = model_->solver()->CheckAssignment(&validation_copy);
            DCHECK(is_solution_correct);

            rows::GexfWriter solution_writer;
            solution_writer.Write(output_file_, *solver_, *model_, *assignment);
            solver_->DisplayPlan(*model_, *assignment);
            SetReturnCode(STATUS_OK);
        } catch (util::ApplicationError &ex) {
            LOG(ERROR) << ex.msg() << std::endl << ex.diagnostic_info();
            SetReturnCode(util::to_exit_code(ex.error_code()));
        } catch (const std::exception &ex) {
            LOG(ERROR) << ex.what() << std::endl;
            SetReturnCode(2);
        } catch (...) {
            LOG(ERROR) << "Unhandled exception";
            SetReturnCode(3);
        }
    }

    std::string output_file_;

    std::shared_ptr<rows::Printer> printer_;

    operations_research::Assignment *initial_assignment_;
    std::unique_ptr<operations_research::RoutingModel> model_;
    std::unique_ptr<rows::SolverWrapper> solver_;
};

class CarerTeam {
public:
    explicit CarerTeam(std::pair<rows::Carer, rows::Diary> member)
            : diary_{member.second} {
        members_.emplace_back(std::move(member));
    }

    void Add(std::pair<rows::Carer, rows::Diary> member) {
        DCHECK(std::find_if(std::cbegin(members_), std::cend(members_),
                            [&member](const std::pair<rows::Carer, rows::Diary> &local_member) -> bool {
                                return local_member.first == member.first;
                            }) == std::cend(members_));

        diary_ = diary_.Intersect(member.second);
        members_.emplace_back(std::move(member));
    }

    std::size_t size() const {
        return members_.size();
    }

    std::vector<rows::Carer> Members() const {
        std::vector<rows::Carer> result;
        for (const auto &member : members_) {
            result.push_back(member.first);
        }
        return result;
    }

    const std::vector<std::pair<rows::Carer, rows::Diary> > &FullMembers() const {
        return members_;
    };

    std::vector<rows::Carer> AvailableMembers(const boost::posix_time::ptime date_time,
                                              const boost::posix_time::time_duration &adjustment) const {
        std::vector<rows::Carer> result;
        for (const auto &member : members_) {
            if (member.second.IsAvailable(date_time, adjustment)) {
                result.push_back(member.first);
            }
        }
        return result;
    }


    const rows::Diary &Diary() const {
        return diary_;
    }

private:
    rows::Diary diary_;
    std::vector<std::pair<rows::Carer, rows::Diary> > members_;
};

class TwoStepSchedulingWorker : public SchedulingWorker {
public:
    explicit TwoStepSchedulingWorker(std::shared_ptr<rows::Printer> printer) :
            printer_{std::move(printer)} {}

    void Run() override {
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

        auto routing_parameters = CreateEngineConfig(FLAGS_maps);
        auto first_step_search_params = rows::SolverWrapper::CreateSearchParameters();
        first_step_search_params.set_time_limit_ms(20 * 1000);
        std::unique_ptr<rows::SolverWrapper> first_stage_wrapper
                = std::make_unique<rows::SingleStepSolver>(sub_problem,
                                                           routing_parameters,
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
                = std::make_unique<rows::TwoStepSolver>(problem_, routing_parameters, second_step_search_params);

        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > first_step_solution;
        first_step_model->AssignmentToRoutes(*first_step_assignment, &first_step_solution);

        // TODO: make sure that symmetry breaking constraint is now respected
        // a carer with smaller vehicle number, gets visit with a smaller number
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

        const auto locks_applied = second_stage_model->ApplyLocksToAllVehicles(second_step_locks, false);
        DCHECK(locks_applied);
        DCHECK(second_stage_model->PreAssignment());

        operations_research::Assignment const *second_stage_assignment
                = second_stage_model->SolveFromAssignmentWithParameters(computed_assignment, second_step_search_params);

        if (second_stage_assignment == nullptr) {
            throw util::ApplicationError("No second stage solution found.", util::ErrorCode::ERROR);
        }

        auto third_step_routing_parameters = rows::SolverWrapper::CreateSearchParameters();

        operations_research::Assignment second_validation_copy{second_stage_assignment};
        const auto is_second_solution_correct = second_stage_model->solver()->CheckAssignment(&second_validation_copy);
        DCHECK(is_second_solution_correct);

        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > third_stage_initial_routes;
        second_stage_model->AssignmentToRoutes(*second_stage_assignment, &third_stage_initial_routes);

        second_stage_model.release();

        std::unique_ptr<rows::SolverWrapper> third_stage_wrapper
                = std::make_unique<rows::TwoStepSolver>(problem_, routing_parameters, second_step_search_params);

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

    bool Init() {
        problem_ = LoadReducedProblem(printer_);
        return true;
    }

private:
    std::vector<CarerTeam> GetCarerTeams(const rows::Problem &problem) {
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
        for (auto carer_diary_it = std::begin(carer_diaries); carer_diary_it != carer_diary_it_end; ++carer_diary_it) {
            if (!processed_carers.insert(carer_diary_it->first).second) {
                continue;
            }

            CarerTeam team{*carer_diary_it};

            // while there is available space and are free carers continue looking for a suitable match
            boost::optional<std::pair<rows::Carer, rows::Diary> > best_match = boost::none;
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

    std::shared_ptr<rows::Printer> printer_;

    rows::Problem problem_;
};

void ChatBot(SchedulingWorker &worker) {
    std::regex non_printable_character_pattern{"[\\W]"};

    std::string line;
    while (true) {
        std::getline(std::cin, line);
        util::string::Strip(line);
        util::string::ToLower(line);

        if (!line.empty() && line == "stop") {
            worker.Cancel();
            break;
        }

        line.clear();
    }
}

std::shared_ptr<rows::Printer> CreatePrinter() {
    auto format_to_use = FLAGS_console_format;
    util::string::Strip(format_to_use);
    util::string::ToLower(format_to_use);
    if (format_to_use == JSON_FORMAT) {
        return std::make_shared<rows::JsonPrinter>();
    }

    if (format_to_use == TEXT_FORMAT) {
        return std::make_shared<rows::ConsolePrinter>();
    }

    throw util::ApplicationError("Unknown console format.", util::ErrorCode::ERROR);
}

int RunSingleStepSchedulingWorker() {
    std::shared_ptr<rows::Printer> printer = CreatePrinter();

    SingleStepSchedulingWorker worker{printer};
    if (worker.Init(FLAGS_problem,
                    FLAGS_maps,
                    FLAGS_solution,
                    FLAGS_time_limit,
                    FLAGS_solutions_limit,
                    FLAGS_output)) {

        worker.Start();
        std::thread chat_thread(ChatBot, std::ref(worker));
        chat_thread.detach();
        worker.Join();
    }

    return worker.ReturnCode();
}

int RunTwoStepSchedulingWorker() {
    std::shared_ptr<rows::Printer> printer = CreatePrinter();

    TwoStepSchedulingWorker worker{printer};
    if (worker.Init()) {
        worker.Start();
        std::thread chat_thread(ChatBot, std::ref(worker));
        chat_thread.detach();
        worker.Join();
    }

    return worker.ReturnCode();
}


int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    return RunTwoStepSchedulingWorker();
}