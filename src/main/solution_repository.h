#ifndef ROWS_SOLUTIONREPOSITORY_H
#define ROWS_SOLUTIONREPOSITORY_H

#include <vector>
#include <memory>

#include <boost/filesystem.hpp>

#include <ortools/constraint_solver/routing.h>

namespace rows {

    class SolutionRepository {
    public:
        SolutionRepository();

        ~SolutionRepository();

        void Store(operations_research::RoutingModel const *model);

        void Store(std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > routes);

        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > GetSolution() const;

        operations_research::Assignment const *GetAssignment(operations_research::RoutingModel *model) const;

        boost::filesystem::path solution_file() const;

    private:
        boost::filesystem::path solution_file_;
        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > solution_;
    };
}


#endif //ROWS_SOLUTIONREPOSITORY_H
