#include "declined_visit_evaluator.h"

#include "problem_data.h"

rows::DeclinedVisitEvaluator::DeclinedVisitEvaluator(const rows::ProblemData &problem_data,
                                                     const operations_research::RoutingIndexManager &index_manager)
        : weights_(problem_data.nodes(), 0) {

    for (operations_research::RoutingIndexManager::NodeIndex visit_node{1}; visit_node < problem_data.nodes(); ++visit_node) {
        const auto &visit = problem_data.NodeToVisit(visit_node);
        const auto &visit_nodes = problem_data.GetNodes(visit);
        CHECK_LE(visit_nodes.size(), 2);
        CHECK_GE(visit_nodes.size(), 1);

        weights_.at(index_manager.NodeToIndex(visit_node)) = visit_nodes.size() == 1 ? 2 : 1;
    }
}

int64 rows::DeclinedVisitEvaluator::GetThreshold(const std::vector<std::vector<int64>> &routes) const {
    int64 result = 0;

    for (const auto &route : routes) {
        for (const auto node : route) {
            result += weights_.at(node);
        }
    }

    return result;
}

int64 rows::DeclinedVisitEvaluator::GetDroppedVisits(const operations_research::RoutingModel &model) const{
    int64 weighed_dropped_visits = 0;
    for (int order = 1; order < model.nodes(); ++order) {
        if (model.NextVar(order)->Value() == order) {
            weighed_dropped_visits += weights_.at(order);
        }
    }
    CHECK_EQ(weighed_dropped_visits % 2, 0);

    return weighed_dropped_visits / 2;
}

int64 rows::DeclinedVisitEvaluator::Weight(int64 index) const {
    return weights_.at(index);
}
