#include "routing_operations.h"

#include <boost/algorithm/string/join.hpp>

int rows::RoutingOperations::Remove(std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> &routes,
                                    operations_research::RoutingModel::NodeIndex node) const {
    auto changes = 0;
    for (auto &route : routes) {
        while (true) {
            auto find_it = std::find(std::begin(route), std::end(route), node);
            if (find_it == std::end(route)) {
                break;
            }

            route.erase(find_it);
            ++changes;
        }
    }
    return changes;
}

int rows::RoutingOperations::Swap(std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> &routes,
                                  operations_research::RoutingModel::NodeIndex left,
                                  operations_research::RoutingModel::NodeIndex right) const {
    auto changed = 0;
    for (auto &route : routes) {
        const auto route_size = route.size();
        for (auto route_index = 0; route_index < route_size; ++route_index) {
            if (route[route_index] == left) {
                route[route_index] = right;
                ++changed;
            } else if (route[route_index] == right) {
                route[route_index] = left;
                ++changed;
            }
        }
    }
    changed;
}

int rows::RoutingOperations::Replace(std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> &routes,
                                     operations_research::RoutingModel::NodeIndex from,
                                     operations_research::RoutingModel::NodeIndex to, std::size_t route_index) const {
    auto changed = 0;
    auto &route = routes[route_index];
    const auto route_size = route.size();
    for (auto node_index = 0; node_index < route_size; ++node_index) {
        if (route[node_index] == from) {
            route[node_index] = to;
            ++changed;
        }
    }
    return changed;
}

void rows::RoutingOperations::PrintRoutes(std::shared_ptr<rows::Printer> printer,
                                          const std::vector<std::vector<operations_research::RoutingModel::NodeIndex>> &routes) const {
    for (const auto &route : routes) {
        std::vector<std::string> node_strings;
        for (const auto node : route) {
            node_strings.emplace_back(std::to_string(node.value()));
        }

        printer->operator<<(boost::algorithm::join(node_strings, "->"));
    }
}
