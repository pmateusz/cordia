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

        void Store(std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > routes);

        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > GetSolution() const;

    private:
        std::vector<std::vector<operations_research::RoutingModel::NodeIndex> > solution_;
    };
}


#endif //ROWS_SOLUTIONREPOSITORY_H
