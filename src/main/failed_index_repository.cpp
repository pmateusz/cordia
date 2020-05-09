#include "failed_index_repository.h"

void rows::FailedIndexRepository::Emplace(int64 index) {
    indices_.emplace(index);
}

const std::unordered_set<int64> &rows::FailedIndexRepository::Indices() const {
    return indices_;
}

void rows::FailedIndexRepository::Clear() {
    indices_.clear();
}
