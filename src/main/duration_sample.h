#ifndef ROWS_DURATION_SAMPLE_H
#define ROWS_DURATION_SAMPLE_H


#include <ortools/constraint_solver/routing.h>

#include "history.h"
#include "problem_data.h"
#include "solver_wrapper.h"

namespace rows {

    class DurationSample {
    public:
        DurationSample(const SolverWrapper &solver, const History &history, const operations_research::RoutingDimension *dimension);

        inline std::size_t size() const { return num_dates_; }

        inline std::size_t num_indices() const { return start_min_.size(); }

        inline int64 start_min(int64 index) const { return start_min_.at(index); }

        inline int64 start_max(int64 index) const { return start_max_.at(index); }

        inline int64 duration(int64 index, std::size_t scenario) const { return duration_sample_.at(index).at(scenario); }

        inline bool is_visit(int64 index) const { return visit_indices_.find(index) != std::cend(visit_indices_); }

        inline bool has_sibling(int64 index) const { return sibling_index_.find(index) != std::cend(sibling_index_); }

        int64 sibling(int64 index) const;

    private:
        std::vector<boost::gregorian::date> dates_;
        std::size_t num_dates_;

        std::unordered_map<boost::gregorian::date, std::size_t> date_position_;

        std::vector<int64> start_min_;
        std::vector<int64> start_max_;

        std::vector<std::vector<int64> > duration_sample_;

        std::unordered_map<int64, int64> sibling_index_;
        std::unordered_set<int64> visit_indices_;
    };
}


#endif //ROWS_DURATION_SAMPLE_H
