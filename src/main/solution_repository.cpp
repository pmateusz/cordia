#include "solution_repository.h"

rows::SolutionRepository::SolutionRepository()
        : solution_{}
        {}

rows::SolutionRepository::~SolutionRepository() {}

void rows::SolutionRepository::Store(std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > solution) {
    solution_.clear();
    solution_.swap(solution);
}

std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > rows::SolutionRepository::GetSolution() const {
    return solution_;
}
