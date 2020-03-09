#include <ortools/constraint_solver/routing_parameters.h>
#include "single_step_worker.h"

rows::SingleStepSchedulingWorker::SingleStepSchedulingWorker(std::shared_ptr<rows::Printer> printer) :
        printer_{std::move(printer)},
        initial_assignment_{nullptr} {}

rows::SingleStepSchedulingWorker::~SingleStepSchedulingWorker() {
    if (model_) {
        model_.reset();
    }

    if (solver_) {
        solver_.reset();
    }

    initial_assignment_ = nullptr;
}

bool rows::SingleStepSchedulingWorker::Init(const rows::ProblemData &problem_data,
                                            boost::optional<rows::Solution> past_solution,
                                            const operations_research::RoutingSearchParameters &search_parameters,
                                            std::string output_file) {
    try {
        solver_ = std::make_unique<rows::SingleStepSolver>(problem_data, search_parameters);
        model_ = std::make_unique<operations_research::RoutingModel>(solver_->index_manager());

        solver_->ConfigureModel(*model_, printer_, CancelToken(), 1.0);
        VLOG(1) << "Completed routing model configuration with status: " << solver_->GetModelStatus(model_->status());
        if (past_solution) {
            VLOG(1) << "Starting with a solution.";
            VLOG(1) << past_solution->DebugStatus(*solver_, *model_);
            const auto solution_to_use = solver_->ResolveValidationErrors(past_solution.get(), *model_);
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

bool rows::SingleStepSchedulingWorker::Init(const rows::ProblemData &problem_data,
                                            const std::string &output_file,
                                            const boost::posix_time::time_duration &visit_time_window,
                                            const boost::posix_time::time_duration &break_time_window,
                                            const boost::posix_time::time_duration &begin_end_shift_time_extension,
                                            const boost::posix_time::time_duration &opt_time_limit,
                                            double cost_normalization_factor) {
    try {
        auto search_params = operations_research::DefaultRoutingSearchParameters();
        search_params.set_first_solution_strategy(operations_research::FirstSolutionStrategy_Value_ALL_UNPERFORMED);
        search_params.mutable_local_search_operators()->set_use_full_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_path_lns(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_exchange_subtrip(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_relocate_expensive_chain(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_light_relocate_pair(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_relocate(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_exchange(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_exchange_pair(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_extended_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.mutable_local_search_operators()->set_use_node_pair_swap_active(operations_research::OptionalBoolean::BOOL_TRUE);
        search_params.set_use_full_propagation(true);

        LOG(INFO) << opt_time_limit;

        solver_ = std::make_unique<rows::SingleStepSolver>(problem_data,
                                                           search_params,
                                                           visit_time_window,
                                                           break_time_window,
                                                           begin_end_shift_time_extension,
                                                           opt_time_limit);

        index_manager_ = std::make_unique<operations_research::RoutingIndexManager>(solver_->nodes(),
                                                                                    solver_->vehicles(),
                                                                                    rows::RealProblemData::DEPOT);
        model_ = std::make_unique<operations_research::RoutingModel>(*index_manager_);

        solver_->ConfigureModel(*model_, printer_, CancelToken(), cost_normalization_factor);
        VLOG(1) << "Completed routing model configuration with status: " << solver_->GetModelStatus(model_->status());

        output_file_ = output_file;
        return true;
    } catch (util::ApplicationError &ex) {
        LOG(ERROR) << ex.msg() << std::endl << ex.diagnostic_info();
        SetReturnCode(util::to_exit_code(ex.error_code()));
        return false;
    }
}

void rows::SingleStepSchedulingWorker::Run() {
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
                << "\nModel status: " << solver_->GetModelStatus(model_->status());

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
