#include "real_problem_data.h"
#include "problem.h"

#include <glog/logging.h>

const operations_research::RoutingIndexManager::NodeIndex rows::RealProblemData::DEPOT{0};

rows::RealProblemData::RealProblemData(const rows::Problem &problem) {
    node_index_.emplace(DEPOT, CalendarVisit()); // depot visit

    // visit that needs multiple carers is referenced by multiple nodes
    // all such nodes must be either performed or unperformed
    operations_research::RoutingNodeIndex current_visit_node{1};
    for (const auto &visit : problem.visits()) {
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
