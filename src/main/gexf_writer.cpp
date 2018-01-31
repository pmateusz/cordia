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

        const auto depot_id = gexf.DepotId(SolverWrapper::DEPOT);
        gexf.AddNode(depot_id, "depot");

//        gexf.SetNodeValue(SolverWrapper::DEPOT, ID, depot_id);
        gexf.SetNodeValue(depot_id, LATITUDE, central_location.latitude());
        gexf.SetNodeValue(depot_id, LONGITUDE, central_location.longitude());
        gexf.SetNodeValue(depot_id, TYPE, VISIT_NODE);

        for (operations_research::RoutingModel::NodeIndex visit_index{1};
             visit_index < model.nodes();
             ++visit_index) {
            const auto visit_id = gexf.VisitId(visit_index);
            const auto &visit = solver.CalendarVisit(visit_index);

            gexf.AddNode(visit_id, (boost::format("visit %1%") % visit_index.value()).str());
            gexf.SetNodeValue(visit_id, TYPE, VISIT_NODE);
//            gexf.SetNodeLabel(visit_index, visit.);
//            gexf.SetNodeValue(visit_index, ID, visit_id);
            if (visit.location()) {
                gexf.SetNodeValue(visit_id, LATITUDE, visit.location().get().latitude());
                gexf.SetNodeValue(visit_id, LONGITUDE, visit.location().get().longitude());
            }
            if (solution.Value(model.NextVar(visit_index.value())) == visit_index.value()) {
                gexf.SetNodeValue(visit_id, DROPPED, TRUE_VALUE);
            }

            gexf.SetNodeValue(visit_id, START_TIME, visit.datetime().time_of_day());
            gexf.SetNodeValue(visit_id, DURATION, visit.duration());
        }

        operations_research::RoutingDimension *time_dim = model.GetMutableDimension(
                rows::SolverWrapper::TIME_DIMENSION);

        static const RouteValidator validator{};

        for (operations_research::RoutingModel::NodeIndex carer_index{0};
             carer_index < model.vehicles();
             ++carer_index) {
            const auto &carer = solver.Carer(carer_index);
            const auto carer_id = gexf.CarerId(carer_index);

            std::vector<rows::ScheduledVisit> route;

            gexf.AddNode(carer_id, (boost::format("carer %1%") % carer_index.value()).str());
            gexf.SetNodeValue(carer_id, ID, carer.sap_number());
            gexf.SetNodeValue(carer_id, TYPE, CARER_NODE);
            gexf.SetNodeValue(carer_id, SAP_NUMBER, carer.sap_number());

            if (!model.IsVehicleUsed(solution, carer_index.value())) {
                gexf.SetNodeValue(carer_id, DROPPED, TRUE_VALUE);
                continue;
            }

            auto start_visit_index = model.Start(carer_index.value());
            DCHECK(!model.IsEnd(solution.Value(model.NextVar(start_visit_index))));

            // TODO: extract time and continuity of care from the solution
            // TODO: rewrite continuity of care requirement to maximize a service user cumulative satisfaction not a visit satisfaction
            while (true) {
// information about slack variables
                operations_research::IntVar *const time_var = time_dim->CumulVar(start_visit_index);
                LOG(INFO) << boost::posix_time::seconds(time_var->Min());
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

                const auto start_visit_node = model.IndexToNode(start_visit_index);
                std::string start_visit_id;

                if (start_visit_node == SolverWrapper::DEPOT) {
                    start_visit_id = gexf.DepotId(start_visit_node);
                } else {
                    start_visit_id = gexf.VisitId(start_visit_node);

                    const auto &calendar_visit = solver.CalendarVisit(start_visit_node);
                    route.emplace_back(ScheduledVisit::VisitType::UNKNOWN, carer, calendar_visit);

                    const auto carer_visit_edge_id = gexf.EdgeId(carer_id, start_visit_id, "c");
                    gexf.AddEdge(carer_visit_edge_id,
                                 carer_id,
                                 start_visit_id,
                                 (boost::format("Carer %1% does visit %2%")
                                  % carer_index
                                  % start_visit_node).str());
                }

                if (model.IsEnd(start_visit_index)) { break; }

                const auto end_visit_index = solution.Value(model.NextVar(start_visit_index));
                const auto end_visit_node = model.IndexToNode(end_visit_index);

                std::string end_visit_id;
                if (end_visit_node == SolverWrapper::DEPOT) {
                    end_visit_id = gexf.DepotId(end_visit_node);
                } else {
                    end_visit_id = gexf.VisitId(end_visit_node);
                }

                const auto visit_visit_edge_id = gexf.EdgeId(start_visit_id, end_visit_id, "v");
                gexf.AddEdge(visit_visit_edge_id,
                             start_visit_id,
                             end_visit_id,
                             (boost::format("Visit %1% after %2%")
                              % start_visit_node
                              % end_visit_node).str());

                const auto travel_time = solver.Distance(start_visit_node, end_visit_node);
                DCHECK_GE(travel_time, 0);
                if (!model.IsEnd(end_visit_index)) {
                    gexf.SetNodeValue(end_visit_id, ASSIGNED_CARER, carer.sap_number());
                }

                gexf.SetEdgeValue(visit_visit_edge_id,
                                  TRAVEL_TIME,
                                  boost::posix_time::seconds(travel_time));
                start_visit_index = end_visit_index;
            }

            if (route.empty()) {
                continue;
            }

            gexf.SetNodeValue(carer_id, UTIL_VISITS_COUNT, std::to_string(route.size()));
            const auto validation_result = validator.Validate(rows::Route(carer, route), solver);

            if (validation_result.error()) {
                LOG(ERROR) << (boost::format("Route %1% is invalid %2%")
                               % carer
                               % *validation_result.error()).str();
            } else {
                const auto &metrics = validation_result.metrics();
                gexf.SetNodeValue(carer_id, UTIL_AVAILABLE_TIME, metrics.available_time());
                gexf.SetNodeValue(carer_id, UTIL_SERVICE_TIME, metrics.service_time());
                gexf.SetNodeValue(carer_id, UTIL_IDLE_TIME, metrics.idle_time());
                gexf.SetNodeValue(carer_id, UTIL_TRAVEL_TIME, metrics.travel_time());

                const auto work_duration = metrics.service_time() + metrics.travel_time();
                DCHECK_LE(work_duration, metrics.available_time());
                if (work_duration.total_seconds() > 0) {
                    const auto relative_duration = static_cast<double>(work_duration.total_seconds())
                                                   / metrics.available_time().total_seconds();
                    gexf.SetNodeValue(carer_id, UTIL_RELATIVE, std::to_string(relative_duration));
                    gexf.SetNodeValue(carer_id, UTIL_ABSOLUTE_TIME, work_duration);
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

    void GexfWriter::GexfEnvironmentWrapper::AddNode(const std::string &node_id,
                                                     const std::string &label) {
        CHECK(!env_ptr_->getDirectedGraph().containsNode(node_id));

        env_ptr_->getDirectedGraph().addNode(node_id);
        env_ptr_->getData().setNodeLabel(node_id, label);
    }

    void GexfWriter::GexfEnvironmentWrapper::Write(const boost::filesystem::path &file_path) {
        DCHECK(env_ptr_->checkIntegrity());

        libgexf::FileWriter file_writer(file_path.string(), env_ptr_.get());
        file_writer.write();
    }

    void GexfWriter::GexfEnvironmentWrapper::AddEdge(const std::string &edge_id,
                                                     const std::string &from_id,
                                                     const std::string &to_id,
                                                     const std::string &label) {
        env_ptr_->getDirectedGraph().addEdge(edge_id, from_id, to_id);
        env_ptr_->getData().setEdgeLabel(edge_id, label);
    }

    void GexfWriter::GexfEnvironmentWrapper::SetEdgeValue(const std::string &edge_id,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          const boost::posix_time::time_duration &value) {
        env_ptr_->getData().setEdgeValue(edge_id, attribute.Id, boost::posix_time::to_simple_string(value));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeValue(const std::string &node_id,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          const boost::posix_time::time_duration &value) {
        SetNodeValue(node_id, attribute, boost::posix_time::to_simple_string(value));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeValue(const std::string &node_id,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          const osrm::util::FixedLongitude &value) {
        SetNodeValue(node_id, attribute, util::to_simple_string(osrm::util::toFloating(value)));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeValue(const std::string &node_id,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          const osrm::util::FixedLatitude &value) {
        SetNodeValue(node_id, attribute, util::to_simple_string(osrm::util::toFloating(value)));
    }

    void GexfWriter::GexfEnvironmentWrapper::SetNodeValue(const std::string &node_id,
                                                          const GexfWriter::GephiAttributeMeta &attribute,
                                                          const std::string &value) {
        CHECK(env_ptr_->getDirectedGraph().containsNode(node_id));

        env_ptr_->getData().setNodeValue(node_id, attribute.Id, value);
    }

    std::string GexfWriter::GexfEnvironmentWrapper::DepotId(
            operations_research::RoutingModel::NodeIndex depot_index) const {
        return (boost::format("d%1%") % depot_index).str();
    }

    std::string GexfWriter::GexfEnvironmentWrapper::CarerId(
            operations_research::RoutingModel::NodeIndex carer_index) const {
        return (boost::format("c%1%") % carer_index).str();
    }

    std::string GexfWriter::GexfEnvironmentWrapper::VisitId(
            operations_research::RoutingModel::NodeIndex visit_index) const {
        return (boost::format("v%1%") % visit_index).str();
    }

    std::string GexfWriter::GexfEnvironmentWrapper::EdgeId(const std::string &from_id,
                                                           const std::string &to_id,
                                                           const std::string &prefix) const {
        return (boost::format("e%1%%2%%3%")
                % prefix
                % from_id
                % to_id).str();
    }
}
