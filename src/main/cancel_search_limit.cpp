#include "cancel_search_limit.h"

rows::CancelSearchLimit::CancelSearchLimit(std::shared_ptr<const std::atomic<bool> > cancel_token,
                                           operations_research::Solver *solver)
        : SearchLimit(solver),
          cancel_token_{std::move(cancel_token)} {}

bool rows::CancelSearchLimit::Check() {
    return *cancel_token_;
}

void rows::CancelSearchLimit::Init() {}

void rows::CancelSearchLimit::Copy(const operations_research::SearchLimit *limit) {
    auto prototype_limit_ptr = reinterpret_cast<const CancelSearchLimit *>(limit);
    cancel_token_ = prototype_limit_ptr->cancel_token_;
}

operations_research::SearchLimit *rows::CancelSearchLimit::MakeClone() const {
    return solver()->RevAlloc(new CancelSearchLimit(cancel_token_, solver()));
}
