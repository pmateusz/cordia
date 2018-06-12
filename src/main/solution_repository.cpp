#include "solution_repository.h"

rows::SolutionRepository::SolutionRepository()
        : solution_{},
          solution_file_{boost::filesystem::unique_path()} {}

rows::SolutionRepository::~SolutionRepository() {
    const auto solution_file_removed = boost::filesystem::remove(solution_file_);
    LOG_IF(WARNING, !solution_file_removed) << "Failed to remove a solution file " << solution_file_;
}

void rows::SolutionRepository::Store(std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > solution) {
    solution_.clear();
    solution_.swap(solution);
}

std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > rows::SolutionRepository::GetSolution() const {
    return solution_;
}

void rows::SolutionRepository::Store(operations_research::RoutingModel const *model) {
    const auto solution_saved = model->WriteAssignment(solution_file_.string());
    LOG_IF(ERROR, !solution_saved) << "Failed to save a solution o file " << solution_file_;
}

operations_research::Assignment const *rows::SolutionRepository::GetAssignment(
        operations_research::RoutingModel *model) const {
    return model->ReadAssignment(solution_file_.string());
}

boost::filesystem::path rows::SolutionRepository::solution_file() const {
    return solution_file_;
}
