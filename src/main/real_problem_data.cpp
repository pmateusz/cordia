#include "real_problem_data.h"
#include "problem.h"

#include <glog/logging.h>

const operations_research::RoutingIndexManager::NodeIndex rows::RealProblemData::DEPOT{0};

rows::RealProblemData::RealProblemData(Problem problem, std::unique_ptr<CachedLocationContainer> location_container)
        : problem_{std::move(problem)},
          location_container_{std::move(location_container)} {
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
}

int rows::RealProblemData::vehicles() const {
    return static_cast<int>(problem_.carers().size());
}

int rows::RealProblemData::nodes() const {
    return static_cast<int>(node_index_.size());
}

int64 rows::RealProblemData::Distance(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) {
    if (from == DEPOT || to == DEPOT) {
        return 0;
    }

    return location_container_->Distance(NodeToVisit(from).location().get(), NodeToVisit(to).location().get());
}

int64 rows::RealProblemData::ServiceTime(operations_research::RoutingNodeIndex node) {
    if (node == DEPOT) {
        return 0;
    }

    const auto visit = NodeToVisit(node);
    return visit.duration().total_seconds();
}

int64 rows::RealProblemData::ServicePlusTravelTime(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) {
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

const rows::CalendarVisit &rows::RealProblemData::NodeToVisit(const operations_research::RoutingNodeIndex &node) const {
    DCHECK_NE(DEPOT, node);

    return node_index_.at(node);
}

std::pair<operations_research::RoutingNodeIndex, operations_research::RoutingNodeIndex>
rows::RealProblemData::GetNodePair(const rows::CalendarVisit &visit) const {
    const auto &nodes = GetNodes(visit);
    CHECK_EQ(nodes.size(), 2);

    const auto first_node = *std::begin(nodes);
    const auto second_node = *std::next(std::begin(nodes));
    auto first_node_to_use = first_node;
    auto second_node_to_use = second_node;
    if (first_node_to_use > second_node_to_use) {
        std::swap(first_node_to_use, second_node_to_use);
    }

    return std::make_pair(first_node_to_use, second_node_to_use);
}
