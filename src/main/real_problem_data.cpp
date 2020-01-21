#include "real_problem_data.h"
#include "problem.h"

#include <glog/logging.h>

std::vector<rows::Location> DistinctLocations(const rows::Problem &problem) {
    std::unordered_set<rows::Location> locations;
    for (const auto &visit : problem.visits()) {
        const auto &location_opt = visit.location();
        if (location_opt) {
            locations.insert(location_opt.get());
        }
    }

    return {std::begin(locations), std::end(locations)};
}

const int64 rows::RealProblemData::SECONDS_IN_DIMENSION = 24 * 3600 + 2 * 3600;

rows::RealProblemData::RealProblemData(Problem problem, std::unique_ptr<CachedLocationContainer> location_container)
        : problem_{std::move(problem)},
          location_container_{std::move(location_container)},
          start_horizon_{boost::posix_time::max_date_time} {
    node_index_.emplace(DEPOT, CalendarVisit()); // depot visit

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    operations_research::RoutingNodeIndex current_visit_node{1};
    for (const auto &visit : problem_.visits()) {
        DCHECK_GT(visit.carer_count(), 0);

        auto insert_pair = visit_index_.emplace(visit, std::vector<operations_research::RoutingNodeIndex>{});
        if (!insert_pair.second) {
            // skip duplicate
            continue;
        }

        auto &node_index_vec = insert_pair.first->second;
        for (auto carer_count = 0; carer_count < visit.carer_count(); ++carer_count, ++current_visit_node) {
            node_index_vec.emplace_back(current_visit_node);
            node_index_.emplace(current_visit_node, visit);
        }
    }
    DCHECK_EQ(current_visit_node.value(), node_index_.size());

    location_container_->ComputeDistances();

    for (const auto &visit : problem_.visits()) {
        boost::posix_time::ptime datetime{visit.datetime().date()};
        start_horizon_ = std::min(start_horizon_, datetime);
    }
}

int rows::RealProblemData::vehicles() const {
    return static_cast<int>(problem_.carers().size());
}

int rows::RealProblemData::nodes() const {
    return static_cast<int>(node_index_.size());
}

boost::posix_time::time_duration rows::RealProblemData::VisitStart(operations_research::RoutingNodeIndex node) const {
    return NodeToVisit(node).datetime() - start_horizon_;
}

boost::posix_time::time_duration rows::RealProblemData::TotalWorkingHours(int vehicle, boost::gregorian::date date) const {
    const auto &carer = problem_.carers().at(vehicle).first;
    const auto &diary_opt = problem_.diary(carer, date);

    if (diary_opt.is_initialized()) {
        const auto &diary = diary_opt.get();
        return diary.duration();
    }

    return boost::posix_time::seconds(0);
}

int64 rows::RealProblemData::Distance(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const {
    if (from == DEPOT || to == DEPOT) {
        return 0;
    }

    return location_container_->Distance(NodeToVisit(from).location().get(), NodeToVisit(to).location().get());
}

int64 rows::RealProblemData::ServiceTime(operations_research::RoutingNodeIndex node) const {
    if (node == DEPOT) {
        return 0;
    }

    const auto visit = NodeToVisit(node);
    return visit.duration().total_seconds();
}

int64 rows::RealProblemData::ServicePlusTravelTime(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const {
    if (from == DEPOT) {
        return 0;
    }

    const auto service_time = ServiceTime(from);
    const auto travel_time = Distance(from, to);
    return service_time + travel_time;
}

const std::vector<operations_research::RoutingNodeIndex> &rows::RealProblemData::GetNodes(const rows::CalendarVisit &visit) const {
    const auto find_it = visit_index_.find(visit);
    CHECK(find_it != std::end(visit_index_));
    CHECK(!find_it->second.empty());
    return find_it->second;
}

const std::vector<operations_research::RoutingNodeIndex> &rows::RealProblemData::GetNodes(operations_research::RoutingNodeIndex node) const {
    return GetNodes(NodeToVisit(node));
}

const rows::CalendarVisit &rows::RealProblemData::NodeToVisit(const operations_research::RoutingNodeIndex &node) const {
    DCHECK_NE(DEPOT, node);

    return node_index_.at(node);
}

int64 rows::RealProblemData::GetDroppedVisitPenalty() const {
    const auto distances = location_container_->LargestDistances(3);
    return std::accumulate(std::cbegin(distances), std::cend(distances), static_cast<int64>(1));
}

bool rows::RealProblemData::Contains(const rows::CalendarVisit &visit) const {
    return visit_index_.find(visit) != std::end(visit_index_);
}

rows::RealProblemDataFactory::RealProblemDataFactory(osrm::EngineConfig engine_config)
        : engine_config_{std::move(engine_config)} {}

std::shared_ptr<rows::ProblemData> rows::RealProblemDataFactory::makeProblem(rows::Problem problem) const {
    const auto locations = DistinctLocations(problem);
    return std::make_shared<RealProblemData>(problem, std::make_unique<CachedLocationContainer>(std::begin(locations),
                                                                                                std::end(locations),
                                                                                                std::make_unique<RealLocationContainer>(
                                                                                                        engine_config_)));
}


