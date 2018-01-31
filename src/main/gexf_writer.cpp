#include "gexf_writer.h"

#include "util/pretty_print.h"
#include "solver_wrapper.h"
#include "route_validator.h"

namespace rows {

    const GexfWriter::GephiAttributeMeta GexfWriter::ID{"0", "id", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LONGITUDE{"1", "longitude", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LATITUDE{"2", "latitude", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::TYPE{"3", "type", "string", "unknown"};
    const GexfWriter::GephiAttributeMeta GexfWriter::ASSIGNED_CARER{"4", "assigned_carer", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::DROPPED{"5", "dropped", "bool", "false"};

    const GexfWriter::GephiAttributeMeta GexfWriter::START_TIME{"6", "start_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::DURATION{"7", "duration", "string", "00:00:00"};

    const GexfWriter::GephiAttributeMeta GexfWriter::TRAVEL_TIME{"8", "travel_time", "string", "00:00:00"};

    const GexfWriter::GephiAttributeMeta GexfWriter::SAP_NUMBER{"9", "sap_number", "string", "unknown"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_RELATIVE{"10", "work_relative", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_ABSOLUTE_TIME{"11", "work_total_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_AVAILABLE_TIME{"12", "work_available_time", "string",
                                                                         "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_SERVICE_TIME{"13", "work_service_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_TRAVEL_TIME{"14", "work_travel_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_IDLE_TIME{"15", "work_idle_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_VISITS_COUNT{"16", "work_visits_count", "long", "0"};

    void GexfWriter::Write(const boost::filesystem::path &file_path,
                           SolverWrapper &solver,
                           const operations_research::RoutingModel &model,
                           const operations_research::Assignment &solution) const {
        static const auto VISIT_NODE = "visit";
        static const auto CARER_NODE = "carer";
        static const auto TRUE_VALUE = "true";

        GexfEnvironmentWrapper gexf;

        const auto central_location = solver.depot();
        gexf.SetDefaultValues(central_location);
        gexf.AddNode(SolverWrapper::DEPOT);

        gexf.SetNodeLabel(SolverWrapper::DEPOT, "depot");
//        gexf.SetNodeValue(SolverWrapper::DEPOT, ID, depot_id);
        gexf.SetNodeValue(SolverWrapper::DEPOT, LATITUDE, central_location.latitude());
        gexf.SetNodeValue(SolverWrapper::DEPOT, LONGITUDE, central_location.longitude());
        gexf.SetNodeValue(SolverWrapper::DEPOT, TYPE, VISIT_NODE);

        for (operations_research::RoutingModel::NodeIndex visit_index{1}; visit_index < model.nodes(); ++visit_index) {
            const auto &visit = solver.CalendarVisit(visit_index);

            gexf.AddNode(visit_index);
//            gexf.SetNodeLabel(visit_index, visit.);
//            gexf.SetNodeValue(visit_index, ID, visit_id);
            gexf.SetNodeValue(visit_index, TYPE, VISIT_NODE);
            if (visit.location()) {
                gexf.SetNodeValue(visit_index, LATITUDE, visit.location().get().latitude());
                gexf.SetNodeValue(visit_index, LONGITUDE, visit.location().get().longitude());
            }
            if (solution.Value(model.NextVar(visit_index.value())) == visit_index.value()) {
                gexf.SetNodeValue(visit_index, DROPPED, TRUE_VALUE);
            }

            gexf.SetNodeValue(visit_index, START_TIME, visit.datetime().time_of_day());
            gexf.SetNodeValue(visit_index, DURATION, visit.duration());
        }

        operations_research::RoutingDimension *time_dim = model.GetMutableDimension(
                rows::SolverWrapper::TIME_DIMENSION);

        static const RouteValidator validator{};
        auto edge_id = 0;

        for (operations_research::RoutingModel::NodeIndex carer_index{0};
             carer_index < model.vehicles();
             ++carer_index) {
            const auto &carer = solver.Carer(carer_index);
            std::vector<rows::ScheduledVisit> route;

            gexf.AddNode(carer_index);
            gexf.SetNodeLabel(carer_index, carer.sap_number());
            gexf.SetNodeValue(carer_index, ID, carer.sap_number());
            gexf.SetNodeValue(carer_index, TYPE, CARER_NODE);
            gexf.SetNodeValue(carer_index, SAP_NUMBER, carer.sap_number());

            if (!model.IsVehicleUsed(solution, carer_index.value())) {
                gexf.SetNodeValue(carer_index, DROPPED, TRUE_VALUE);
                continue;
            }

            auto start_visit = model.Start(carer_index.value());
            DCHECK(!model.IsEnd(solution.Value(model.NextVar(start_visit))));

            // TODO: extract time and continuity of care from the solution
            // TODO: rewrite continuity of care requirement to maximize a service user cumulative satisfaction not a visit satisfaction
            while (true) {
// information about slack variables
                operations_research::IntVar *const time_var = time_dim->CumulVar(start_visit);
                LOG(INFO) << boost::posix_time::seconds(time_var->Value());
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
                const auto &calendar_visit = solver.CalendarVisit(start_visit_node);
                route.emplace_back(ScheduledVisit::VisitType::UNKNOWN, carer, calendar_visit);

                gexf.AddEdge(carer_index, start_visit_node);

                if (model.IsEnd(start_visit)) { break; }

                const auto end_visit = solution.Value(model.NextVar(start_visit));
                const auto end_visit_node = model.IndexToNode(end_visit);
                gexf.AddEdge(start_visit_node, end_visit_node);

                const auto travel_time = solver.Distance(start_visit_node, end_visit_node);
                DCHECK_GE(travel_time, 0);
                if (!model.IsEnd(end_visit)) {
                    gexf.SetNodeValue(end_visit_node, ASSIGNED_CARER, carer.sap_number());
                }

                gexf.SetEdgeValue(start_visit_node,
                                  end_visit_node,
                                  TRAVEL_TIME,
                                  boost::posix_time::seconds(travel_time));
                start_visit = end_visit;
            }

            if (route.empty()) {
                continue;
            }

            gexf.SetNodeValue(carer_index, UTIL_VISITS_COUNT, std::to_string(route.size()));
            const auto validation_result = validator.Validate(rows::Route(carer, route), solver);

            if (validation_result.error()) {
                LOG(ERROR) << (boost::format("Route %1% is invalid %2%")
                               % carer
                               % *validation_result.error()).str();
            } else {
                const auto &metrics = validation_result.metrics();
                gexf.SetNodeValue(carer_index, UTIL_AVAILABLE_TIME, metrics.available_time());
                gexf.SetNodeValue(carer_index, UTIL_SERVICE_TIME, metrics.service_time());
                gexf.SetNodeValue(carer_index, UTIL_IDLE_TIME, metrics.idle_time());
                gexf.SetNodeValue(carer_index, UTIL_TRAVEL_TIME, metrics.travel_time());

                const auto work_duration = metrics.service_time() + metrics.travel_time();
                DCHECK_LE(work_duration, metrics.available_time());
                if (work_duration.total_seconds() > 0) {
                    const auto relative_duration = static_cast<double>(work_duration.total_seconds())
                                                   / metrics.available_time().total_seconds();
                    gexf.SetNodeValue(carer_index, UTIL_RELATIVE, std::to_string(relative_duration));
                    gexf.SetNodeValue(carer_index, UTIL_ABSOLUTE_TIME, work_duration);
                }
            }
        }

        gexf.Write(file_path);
    }

    GexfWriter::GexfEnvironmentWrapper::GexfEnvironmentWrapper()
            : env_ptr_(std::make_unique<libgexf::GEXF>()) {}

    void GexfWriter::GexfEnvironmentWrapper::SetDefaultValues(const rows::Location &location) {
        auto &data = env_ptr_->getData();
        for (const auto &attr : {ID, TYPE, DROPPED, START_TIME, DURATION, ASSIGNED_CARER,
                                 SAP_NUMBER,
                                 UTIL_RELATIVE,
                                 UTIL_ABSOLUTE_TIME,
                                 UTIL_AVAILABLE_TIME,
                                 UTIL_SERVICE_TIME,
                                 UTIL_TRAVEL_TIME,
                                 UTIL_IDLE_TIME,
                                 UTIL_VISITS_COUNT}) {
            data.addNodeAttributeColumn(attr.Id, attr.Name, attr.Type);
            data.setNodeAttributeDefault(attr.Id, attr.DefaultValue);
        }

        data.addNodeAttributeColumn(LATITUDE.Id, LATITUDE.Name, LATITUDE.Type);
        data.setNodeAttributeDefault(LATITUDE.Id, util::to_simple_string(osrm::toFloating(location.latitude())));
        data.addNodeAttributeColumn(LONGITUDE.Id, LONGITUDE.Name, LONGITUDE.Type);
        data.setNodeAttributeDefault(LONGITUDE.Id, util::to_simple_string(osrm::toFloating(location.longitude())));
        data.addEdgeAttributeColumn(TRAVEL_TIME.Id, TRAVEL_TIME.Name, TRAVEL_TIME.Type);
        data.setEdgeAttributeDefault(TRAVEL_TIME.Id, TRAVEL_TIME.DefaultValue);
    }

    void GexfWriter::GexfEnvironmentWrapper::AddNode(operations_research::RoutingModel::NodeIndex node_index) {
        auto node_id = std::move(std::to_string(next_node_id_++));
        env_ptr_->getDirectedGraph().addNode(node_id);
        const auto inserted_pair = node_ids_.emplace(std::move(node_index), node_id);
        DCHECK(inserted_pair.second);
    }

    void GexfWriter::GexfEnvironmentWrapper::Write(const boost::filesystem::path &file_path) {
        DCHECK(env_ptr_->checkIntegrity());

        libgexf::FileWriter file_writer(file_path.string(), env_ptr_.get());
        file_writer.write();
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeLabel(operations_research::RoutingModel::NodeIndex index,
                                                          const std::string &value) {
        const auto find_it = node_ids_.find(index);
        DCHECK(find_it != std::end(node_ids_));

        env_ptr_->getData().setNodeLabel(find_it->second, value);
    }

    void GexfWriter::GexfEnvironmentWrapper::AddEdge(operations_research::RoutingModel::NodeIndex from,
                                                     operations_research::RoutingModel::NodeIndex to) {
        auto edge_id = std::move(std::to_string(next_edge_id_++));
        auto from_id = std::move(std::to_string(from.value()));
        auto to_id = std::move(std::to_string(to.value()));

        env_ptr_->getDirectedGraph().addEdge(edge_id, from_id, to_id);
        edge_ids_.emplace(std::move(std::make_pair(from, to)), std::move(edge_id));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetEdgeValue(operations_research::RoutingModel::NodeIndex from,
                                                          operations_research::RoutingModel::NodeIndex to,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          const boost::posix_time::time_duration &value) {
        const std::pair<operations_research::RoutingModel::NodeIndex,
                operations_research::RoutingModel::NodeIndex> edge_pair{from, to};
        auto edge_it = edge_ids_.find(edge_pair);
        DCHECK(edge_it != std::end(edge_ids_));
        env_ptr_->getData().setEdgeValue(edge_it->second, attribute.Id, boost::posix_time::to_simple_string(value));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeValue(operations_research::RoutingModel::NodeIndex index,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          boost::posix_time::time_duration value) {
        SetNodeValue(index, attribute, boost::posix_time::to_simple_string(value));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeValue(operations_research::RoutingModel::NodeIndex index,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          osrm::util::FixedLongitude value) {
        SetNodeValue(index, attribute, util::to_simple_string(osrm::util::toFloating(value)));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeValue(operations_research::RoutingModel::NodeIndex index,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          osrm::util::FixedLatitude value) {
        SetNodeValue(index, attribute, util::to_simple_string(osrm::util::toFloating(value)));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeValue(operations_research::RoutingModel::NodeIndex index,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          std::string value) {
        const auto node_it = node_ids_.find(index);
        DCHECK(node_it != std::end(node_ids_));

        env_ptr_->getData().setNodeValue(node_it->second, attribute.Id, std::move(value));
    }
}
