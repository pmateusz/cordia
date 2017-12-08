#include "gexf_writer.h"

#include <libgexf/gexf.h>
#include <libgexf/libgexf.h>

namespace rows {

    const GexfWriter::GephiAttributeMeta GexfWriter::ID{"0", "id", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LONGITUDE{"1", "position_x", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LATITUDE{"2", "position_y", "double", "0"};

    void GexfWriter::Write(const boost::filesystem::path &file_path,
                           const SolverWrapper &solver,
                           const operations_research::RoutingModel &model,
                           const operations_research::Assignment &solution) const {

        std::unique_ptr<libgexf::GEXF> env_ptr = std::make_unique<libgexf::GEXF>();

        auto &data = env_ptr->getData();
        for (const auto &attr : {ID, LONGITUDE, LATITUDE}) {
            data.addNodeAttributeColumn(attr.Id, attr.Name, attr.Type);
        }

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

        DCHECK(env_ptr->checkIntegrity());

        libgexf::FileWriter file_writer(file_path.string(), env_ptr.get());
        file_writer.write();
    }
}
