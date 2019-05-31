#include "solution_repository.h"

rows::SolutionRepository::SolutionRepository()
        : solution_{} {}

rows::SolutionRepository::~SolutionRepository() {}

void rows::SolutionRepository::Store(std::vector<std::vector<int64> > solution) {
    solution_.clear();
    solution_.swap(solution);
}

std::vector<std::vector<int64> > rows::SolutionRepository::GetSolution() const {
    return solution_;
}
