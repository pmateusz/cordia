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

bool rows::SingleStepSchedulingWorker::Init(rows::Problem problem, osrm::EngineConfig engine_config,
                                            boost::optional<rows::Solution> past_solution,
                                            operations_research::RoutingSearchParameters search_parameters,
                                            std::string output_file) {
    try {
        solver_ = std::make_unique<rows::SingleStepSolver>(problem, engine_config, search_parameters);
        model_ = std::make_unique<operations_research::RoutingModel>(solver_->nodes(),
                                                                     solver_->vehicles(),
                                                                     rows::SolverWrapper::DEPOT);

        solver_->ConfigureModel(*model_, printer_, CancelToken());
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

bool rows::SingleStepSchedulingWorker::Init(const rows::Problem &problem,
                                            osrm::EngineConfig &engine_config,
                                            const std::string &output_file,
                                            const boost::posix_time::time_duration &visit_time_window,
                                            const boost::posix_time::time_duration &break_time_window,
                                            const boost::posix_time::time_duration &begin_end_shift_time_extension,
                                            const boost::posix_time::time_duration &opt_time_limit) {
    try {
        const auto search_params = rows::SolverWrapper::CreateSearchParameters();
        solver_ = std::make_unique<rows::SingleStepSolver>(problem,
                                                           engine_config,
                                                           search_params,
                                                           visit_time_window,
                                                           break_time_window,
                                                           begin_end_shift_time_extension,
                                                           opt_time_limit);
        model_ = std::make_unique<operations_research::RoutingModel>(solver_->nodes(),
                                                                     solver_->vehicles(),
                                                                     rows::SolverWrapper::DEPOT);

        solver_->ConfigureModel(*model_, printer_, CancelToken());
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
        solution_writer.Write(output_file_, *solver_, *model_, *assignment, boost::none);
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
