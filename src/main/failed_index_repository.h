#ifndef ROWS_FAILED_INDEX_REPOSITORY_H
#define ROWS_FAILED_INDEX_REPOSITORY_H

#include <unordered_set>

#include <ortools/constraint_solver/constraint_solveri.h>

namespace rows {

    class FailedIndexRepository {
    public:
        void Emplace(int64 index);

        void Clear();

        const std::unordered_set<int64> &Indices() const;

    private:
        std::unordered_set<int64> indices_;
    };
}

#endif //ROWS_FAILED_INDEX_REPOSITORY_H
