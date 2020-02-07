#include "duration_sample.h"


rows::DurationSample::DurationSample(const rows::SolverWrapper &solver,
                                     const rows::History &history,
                                     const operations_research::RoutingDimension *dimension) {
    const auto &index_manager = solver.index_manager();

    // build indexed sample of historical visit durations
    // build mapping of sibling indices
    std::unordered_map<int64, std::unordered_map<boost::gregorian::date, boost::posix_time::time_duration> > visit_samples;
    for (const auto &visit : solver.problem().visits()) {
        const auto visit_indices = index_manager.NodesToIndices(solver.GetNodes(visit));
        CHECK_EQ(visit_indices.size(), visit.carer_count());
        CHECK_GE(visit_indices.size(), 1);
        CHECK_LE(visit_indices.size(), 2);

        visit_indices_.emplace(visit_indices[0]);
        if (visit_indices.size() == 2) {
            visit_indices_.emplace(visit_indices[1]);

            sibling_index_.emplace(visit_indices[0], visit_indices[1]);
            sibling_index_.emplace(visit_indices[1], visit_indices[0]);
        }

        auto sample = history.get_duration_sample(visit);
        visit_samples.emplace(visit_indices[0], std::move(sample));
    }
    

    // build index of dates
    std::unordered_set<boost::gregorian::date> unique_dates;
    for (const auto &visit_sample_pair: visit_samples) {
        for (const auto &date_duration_pair : visit_sample_pair.second) {
            unique_dates.emplace(date_duration_pair.first);
        }
    }

    dates_ = std::vector<boost::gregorian::date>(std::cbegin(unique_dates), std::cend(unique_dates));
    std::sort(std::begin(dates_), std::end(dates_));
    num_dates_ = dates_.size();

    for (std::size_t position = 0; position < num_dates_; ++position) {
        date_position_[dates_[position]] = position;
    }

    start_min_.resize(index_manager.num_indices());
    start_max_.resize(index_manager.num_indices());
    for (auto index = 0; index < index_manager.num_indices(); ++index) {
        start_min_.at(index) = dimension->CumulVar(index)->Min();
        start_max_.at(index) = dimension->CumulVar(index)->Max();
    }

    // build matrix with visits duration: visit index x date
    duration_sample_.resize(solver.index_manager().num_indices());
    for (const auto &visit_index_sample_pair : visit_samples) {
        const auto &default_visit = solver.NodeToVisit(index_manager.IndexToNode(visit_index_sample_pair.first));
        const auto default_duration = default_visit.duration().total_seconds();

        std::vector<int64> duration;
        duration.resize(num_dates_, default_duration);

        for (const auto &date_duration_pair : visit_index_sample_pair.second) {
            duration.at(date_position_.at(date_duration_pair.first)) = date_duration_pair.second.total_seconds();
        }

        duration_sample_.at(visit_index_sample_pair.first) = std::move(duration);
        const auto sibling_index_it = sibling_index_.find(visit_index_sample_pair.first);
        if (sibling_index_it != std::cend(sibling_index_)) {
            duration_sample_.at(sibling_index_it->second) = duration_sample_.at(sibling_index_it->first);
        }
    }

    for (auto index = 0; index < index_manager.num_indices(); ++index) {
        if (duration_sample_.at(index).empty()) {
            duration_sample_.at(index).resize(num_dates_, 0);
        }
    }
}

int64 rows::DurationSample::sibling(int64 index) const {
    const auto sibling_it = sibling_index_.find(index);
    if (sibling_it != std::cend(sibling_index_)) {
        return sibling_it->second;
    }
    return -1;
}
