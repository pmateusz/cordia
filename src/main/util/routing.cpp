#include "routing.h"

std::vector<std::vector<int64> > util::GetRoutes(const operations_research::RoutingModel &model) {
    std::vector<std::vector<int64> > routes;
    routes.resize(static_cast<std::size_t>(model.vehicles()));
    for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        std::vector<int64> *const vehicle_route = &routes.at(vehicle);
        vehicle_route->clear();

        const int64 first_index = model.Start(vehicle);
        const operations_research::IntVar *const first_var = model.NextVar(first_index);
        int64 current_index = first_var->Value();
        while (!model.IsEnd(current_index)) {
            vehicle_route->push_back(current_index);

            const operations_research::IntVar *const next_var = model.NextVar(current_index);
            current_index = next_var->Value();
        }
    }
    return routes;
}

std::unordered_set<int64> util::GetVisitedNodes(const std::vector<std::vector<int64> > &routes, int64 depot_index) {
    std::unordered_set<int64> visited_nodes;
    for (const auto &route : routes) {
        for (const auto &node : route) {
            const auto inserted_pair = visited_nodes.insert(node);
            CHECK(inserted_pair.second || node != depot_index);
        }
    }
    return visited_nodes;
}

std::size_t util::GetDroppedVisitCount(const operations_research::RoutingModel &model) {
    std::size_t dropped_visits = 0;
    for (int order = 1; order < model.nodes(); ++order) {
        if (model.NextVar(order)->Value() == order) {
            ++dropped_visits;
        }
    }
    return dropped_visits;
}

double util::Cost(const operations_research::RoutingModel &model) {
    return static_cast<double>(model.CostVar()->Value());
}

boost::posix_time::time_duration util::WallTime(operations_research::Solver const *solver) {
    return boost::posix_time::milliseconds(static_cast<int64_t>(solver->wall_time()));
}