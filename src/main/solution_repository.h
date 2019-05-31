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

        void Store(std::vector<std::vector<int64> > routes);

        std::vector<std::vector<int64> > GetSolution() const;

    private:
        std::vector<std::vector<int64> > solution_;
    };
}


#endif //ROWS_SOLUTIONREPOSITORY_H
