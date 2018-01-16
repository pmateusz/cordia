#include "gexf_writer.h"

#include <libgexf/gexf.h>
#include <libgexf/libgexf.h>

#include "util/pretty_print.h"

namespace rows {

    // TODO: verify that the solution does not violate constraints

    const GexfWriter::GephiAttributeMeta GexfWriter::ID{"0", "id", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LONGITUDE{"1", "longitude", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LATITUDE{"2", "latitude", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::TYPE{"3", "type", "string", "unknown"};
    const GexfWriter::GephiAttributeMeta GexfWriter::ASSIGNED_CARER{"4", "assigned_carer", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::DROPPED{"5", "dropped", "bool", "false"};

    const GexfWriter::GephiAttributeMeta GexfWriter::START_TIME{"6", "start_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::DURATION{"7", "duration", "string", "00:00:00"};

    const GexfWriter::GephiAttributeMeta GexfWriter::SAP_NUMBER{"8", "sap_number", "string", "unknown"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTILIZATION_RELATIVE{"9", "work_rel", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTILIZATION_ABSOLUTE{"10", "work_abs", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTILIZATION_AVAILABLE{"11", "work_available", "string",
                                                                           "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTILIZATION_VISITS{"12", "work_visits", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::TRAVEL_TIME{"1", "travel_time", "long", "0"};

    void GexfWriter::Write(const boost::filesystem::path &file_path,
                           SolverWrapper &solver,
                           const operations_research::RoutingModel &model,
                           const operations_research::Assignment &solution) const {
        static const auto VISIT_NODE = "visit";
        static const auto CARER_NODE = "carer";
        static const auto TRUE_VALUE = "true";

        std::unique_ptr<libgexf::GEXF> env_ptr = std::make_unique<libgexf::GEXF>();

        auto &data = env_ptr->getData();
        for (const auto &attr : {ID, TYPE, DROPPED, START_TIME, DURATION, ASSIGNED_CARER,
                                 SAP_NUMBER, UTILIZATION_ABSOLUTE, UTILIZATION_AVAILABLE, UTILIZATION_RELATIVE,
                                 UTILIZATION_VISITS}) {
            data.addNodeAttributeColumn(attr.Id, attr.Name, attr.Type);
            data.setNodeAttributeDefault(attr.Id, attr.DefaultValue);
        }

        const auto central_location = solver.GetCentralLocation();
        data.addNodeAttributeColumn(LATITUDE.Id, LATITUDE.Name, LATITUDE.Type);
        data.setNodeAttributeDefault(LATITUDE.Id, util::to_simple_string(central_location.latitude()));
        data.addNodeAttributeColumn(LONGITUDE.Id, LONGITUDE.Name, LONGITUDE.Type);
        data.setNodeAttributeDefault(LONGITUDE.Id, util::to_simple_string(central_location.longitude()));

        data.addEdgeAttributeColumn(TRAVEL_TIME.Id, TRAVEL_TIME.Name, TRAVEL_TIME.Type);
        data.setEdgeAttributeDefault(TRAVEL_TIME.Id, TRAVEL_TIME.DefaultValue);

        auto &graph = env_ptr->getDirectedGraph();

        long node_id = 0;
        const auto raw_depot_id = node_id;
        std::unordered_map<operations_research::RoutingModel::NodeIndex, std::string> node_ids;
        node_ids.emplace(SolverWrapper::DEPOT, std::to_string(node_id++));

        const auto depot_id = std::to_string(raw_depot_id);
        graph.addNode(depot_id);
        data.setNodeLabel(depot_id, "depot");
        data.setNodeValue(depot_id, ID.Id, depot_id);
        data.setNodeValue(depot_id, LATITUDE.Id, util::to_simple_string(central_location.latitude()));
        data.setNodeValue(depot_id, LONGITUDE.Id, util::to_simple_string(central_location.longitude()));
        data.setNodeValue(depot_id, TYPE.Id, VISIT_NODE);

        for (operations_research::RoutingModel::NodeIndex visit_index{1}; visit_index < model.nodes(); ++visit_index) {
            const auto &visit = solver.CalendarVisit(visit_index);

            const auto raw_visit_id = node_id;
            node_ids.emplace(visit_index, std::to_string(node_id++));
            const auto visit_id = std::to_string(raw_visit_id);

            graph.addNode(visit_id);

            data.setNodeLabel(visit_id, visit_id);
            data.setNodeValue(visit_id, ID.Id, visit_id);
            data.setNodeValue(visit_id, TYPE.Id, VISIT_NODE);
            if (visit.location()) {
                data.setNodeValue(visit_id, LATITUDE.Id, util::to_simple_string(visit.location().value().latitude()));
                data.setNodeValue(visit_id, LONGITUDE.Id, util::to_simple_string(visit.location().value().longitude()));
            }
            if (solution.Value(model.NextVar(visit_index.value())) == visit_index.value()) {
                data.setNodeValue(visit_id, DROPPED.Id, TRUE_VALUE);
            }

            data.setNodeValue(visit_id, START_TIME.Id,
                              boost::posix_time::to_simple_string(visit.datetime().time_of_day()));
            data.setNodeValue(visit_id, DURATION.Id, boost::posix_time::to_simple_string(visit.duration()));
        }

//        operations_research::RoutingDimension *const time_dim = model.GetMutableDimension(
//                rows::SolverWrapper::TIME_DIMENSION);

        // TODO: id allocation should be a class

        auto edge_id = 0;
        for (operations_research::RoutingModel::NodeIndex carer_index{0};
             carer_index < model.vehicles(); ++carer_index) {

            const auto &carer = solver.Carer(carer_index);
            const auto raw_carer_id = node_id;
            node_ids.emplace(carer_index, std::to_string(node_id++));
            const auto carer_id = std::to_string(raw_carer_id);
            graph.addNode(carer_id);

            data.setNodeLabel(carer_id, carer_id);
            data.setNodeValue(carer_id, ID.Id, carer_id);
            data.setNodeValue(carer_id, TYPE.Id, CARER_NODE);

            if (!model.IsVehicleUsed(solution, carer_index.value())) {
                data.setNodeValue(carer_id, DROPPED.Id, TRUE_VALUE);
                continue;
            }

            data.setNodeValue(carer_id, SAP_NUMBER.Id, carer.sap_number());

            auto start_visit = model.Start(carer_index.value());

            DCHECK(!model.IsEnd(solution.Value(model.NextVar(start_visit))));

            boost::posix_time::time_duration work_available{boost::posix_time::seconds(0)};
            const auto diary = solver.Diary(carer_index);
            for (const auto &event : diary.events()) {
                work_available += event.duration();
            }

            auto total_visits = 0;
            boost::posix_time::time_duration work_duration{boost::posix_time::seconds(0)};
            while (true) {
// information about slack variables
//                operations_research::IntVar *const time_var = time_dim->CumulVar(start_visit);
//                operations_research::IntVar *const slack_var = model.IsEnd(start_visit) ? nullptr : time_dim->SlackVar(
//                        start_visit);
//                if (slack_var != nullptr && solution.Contains(slack_var)) {
//                    boost::format("%1% Time(%2%, %3%) Slack(%4%, %5%) -> ")
//                    % start_visit
//                    % solution.Min(time_var) % solution.Max(time_var)
//                    % solution.Min(slack_var) % solution.Max(slack_var);
//                } else {
//                    boost::format("%1% Time(%2%, %3%) ->")
//                    % start_visit
//                    % solution.Min(time_var)
//                    % solution.Max(time_var);
//                }

                const auto carer_visit_edge = std::to_string(edge_id++);

                const auto start_visit_node = model.IndexToNode(start_visit);
                const auto start_visit_id = (*node_ids.find(
                        operations_research::RoutingModel::NodeIndex(start_visit_node))).second;

                graph.addEdge(carer_visit_edge, carer_id, start_visit_id);

                if (model.IsEnd(start_visit)) { break; }

                const auto start_visit_to_end_visit_edge = std::to_string(edge_id++);
                const auto end_visit = solution.Value(model.NextVar(start_visit));
                const auto end_visit_node = model.IndexToNode(end_visit);
                const auto end_visit_id = (*node_ids.find(
                        operations_research::RoutingModel::NodeIndex(end_visit_node))).second;

                graph.addEdge(start_visit_to_end_visit_edge, start_visit_id, end_visit_id);

                const auto travel_time = solver.Distance(start_visit_node, end_visit_node);
                DCHECK_GE(travel_time, 0);
                if (!model.IsEnd(end_visit)) {
                    if (!model.IsStart(start_visit)) {
                        work_duration += boost::posix_time::seconds(travel_time);
                    }

                    const auto &visit = solver.CalendarVisit(end_visit_node);
                    work_duration += visit.duration();

                    data.setNodeValue(end_visit_id, ASSIGNED_CARER.Id, carer_id);

                    ++total_visits;
                }

                data.setEdgeValue(start_visit_to_end_visit_edge, TRAVEL_TIME.Id, std::to_string(travel_time));
                start_visit = end_visit;
            }

            if (total_visits > 0) {
                data.setNodeValue(carer_id, UTILIZATION_VISITS.Id, std::to_string(total_visits));
            }
            data.setNodeValue(carer_id, UTILIZATION_AVAILABLE.Id, boost::posix_time::to_simple_string(work_available));
            data.setNodeValue(carer_id, UTILIZATION_ABSOLUTE.Id, boost::posix_time::to_simple_string(work_duration));

            DCHECK_LE(work_duration, work_available);
            if (work_duration.total_seconds() > 0) {
                const auto relative_duration = static_cast<double>(work_duration.total_seconds())
                                               / work_available.total_seconds();
                data.setNodeValue(carer_id, UTILIZATION_RELATIVE.Id, std::to_string(relative_duration));
            }
        }

        DCHECK(env_ptr->checkIntegrity());

        libgexf::FileWriter file_writer(file_path.string(), env_ptr.get());
        file_writer.write();
    }
}
