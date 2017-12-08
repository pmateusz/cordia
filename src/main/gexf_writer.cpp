#include "gexf_writer.h"

#include <libgexf/gexf.h>
#include <libgexf/libgexf.h>

namespace rows {

    const GexfWriter::GephiAttributeMeta GexfWriter::ID{"0", "id", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LONGITUDE{"1", "position_x", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LATITUDE{"2", "position_y", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::TRAVEL_TIME{"0", "travel_time", "long", "0"};

    void GexfWriter::Write(const boost::filesystem::path &file_path,
                           SolverWrapper &solver,
                           const operations_research::RoutingModel &model,
                           const operations_research::Assignment &solution) const {

        std::unique_ptr<libgexf::GEXF> env_ptr = std::make_unique<libgexf::GEXF>();

        auto &data = env_ptr->getData();
        for (const auto &attr : {ID, LONGITUDE, LATITUDE}) {
            data.addNodeAttributeColumn(attr.Id, attr.Name, attr.Type);
        }

        data.addEdgeAttributeColumn(TRAVEL_TIME.Id, TRAVEL_TIME.Name, TRAVEL_TIME.Type);

        auto &graph = env_ptr->getDirectedGraph();

        // temporary depot located in the James Weir building in Glasgow
        const auto raw_depot_id = std::to_string(SolverWrapper::DEPOT.value());
        graph.addNode(raw_depot_id);
        data.setNodeLabel(raw_depot_id, "depot");
        data.setNodeValue(raw_depot_id, ID.Id, raw_depot_id);
        data.setNodeValue(raw_depot_id, LATITUDE.Id, "55.862");
        data.setNodeValue(raw_depot_id, LONGITUDE.Id, "-4.24539");

        for (int order = 1; order < model.nodes(); ++order) {
            const auto raw_id = std::to_string(order);

            graph.addNode(raw_id);

            const auto &visit = solver.Visit(operations_research::RoutingModel::NodeIndex(order));

            data.setNodeLabel(raw_id, raw_id);
            data.setNodeValue(raw_id, ID.Id, raw_id);
            data.setNodeValue(raw_id, LATITUDE.Id, std::to_string(static_cast<double>(visit.location().latitude())));
            data.setNodeValue(raw_id, LONGITUDE.Id, std::to_string(static_cast<double>(visit.location().longitude())));
//            const auto is_dropped = solution.Value(model.NextVar(order)) == order;
        }

        operations_research::RoutingDimension *const time_dim = model.GetMutableDimension(
                rows::SolverWrapper::TIME_DIMENSION);

        auto edge_id = 0;
        for (int route_number = 0; route_number < model.vehicles(); ++route_number) {
            if (!model.IsVehicleUsed(solution, route_number)) {
                continue;
            }

            auto edge_begin = model.Start(route_number);

            DCHECK(!model.IsEnd(solution.Value(model.NextVar(edge_begin))));

            while (true) {
// TODO: write more information about slack variables
//                operations_research::IntVar *const time_var = time_dim->CumulVar(edge_begin);
//                operations_research::IntVar *const slack_var = model.IsEnd(edge_begin) ? nullptr : time_dim->SlackVar(
//                        edge_begin);
//                if (slack_var != nullptr && solution.Contains(slack_var)) {
//                    boost::format("%1% Time(%2%, %3%) Slack(%4%, %5%) -> ")
//                    % edge_begin
//                    % solution.Min(time_var) % solution.Max(time_var)
//                    % solution.Min(slack_var) % solution.Max(slack_var);
//                } else {
//                    boost::format("%1% Time(%2%, %3%) ->")
//                    % edge_begin
//                    % solution.Min(time_var)
//                    % solution.Max(time_var);
//                }

                if (model.IsEnd(edge_begin)) { break; }
                const auto edge_end = solution.Value(model.NextVar(edge_begin));

                const auto raw_edge_id = std::to_string(edge_id);
                const auto node_begin = model.IndexToNode(edge_begin);
                const auto node_end = model.IndexToNode(edge_end);

                graph.addEdge(raw_edge_id,
                              std::to_string(node_begin.value()),
                              std::to_string(node_end.value()),
                              1.0,
                              libgexf::EDGE_DIRECTED);

                const auto travel_time = solver.Distance(node_begin, node_end);
                data.setEdgeValue(raw_edge_id, TRAVEL_TIME.Id, std::to_string(travel_time));

                edge_begin = edge_end;

                ++edge_id;
            }
        }

        DCHECK(env_ptr->checkIntegrity());

        libgexf::FileWriter file_writer(file_path.string(), env_ptr.get());
        file_writer.write();
    }
}
