#ifndef ROWS_CANCEL_SEARCH_MONITOR_H
#define ROWS_CANCEL_SEARCH_MONITOR_H

#include <memory>
#include <atomic>

#include <ortools/constraint_solver/constraint_solver.h>

namespace rows {

    class CancelSearchLimit : public operations_research::SearchLimit {
    public:
        CancelSearchLimit(std::shared_ptr<const std::atomic<bool> > cancel_token, operations_research::Solver *solver);

        bool Check() override;

        void Init() override;

        void Copy(const SearchLimit *limit) override;

        operations_research::SearchLimit *MakeClone() const override;

    private:
        std::shared_ptr<const std::atomic<bool> > cancel_token_;
    };
}


#endif //ROWS_CANCEL_SEARCH_MONITOR_H
