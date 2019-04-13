#include "gexf_writer.h"

#include "util/pretty_print.h"

namespace rows {

    const GexfWriter::GephiAttributeMeta GexfWriter::ID{"0", "id", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LONGITUDE{"1", "longitude", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::LATITUDE{"2", "latitude", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::TYPE{"3", "type", "string", "unknown"};
    const GexfWriter::GephiAttributeMeta GexfWriter::ASSIGNED_CARER{"4", "assigned_carer", "long", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::DROPPED{"5", "dropped", "bool", "false"};
    const GexfWriter::GephiAttributeMeta GexfWriter::SATISFACTION{"6", "satisfaction", "double", "0.0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::USER{"7", "user", "string", "unknown"};
    const GexfWriter::GephiAttributeMeta GexfWriter::CARER_COUNT{"19", "carer_count", "long", "0"};

    const GexfWriter::GephiAttributeMeta GexfWriter::START_TIME{"8", "start_time", "string", "2000-Jan-01 00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::DURATION{"9", "duration", "string", "00:00:00"};

    const GexfWriter::GephiAttributeMeta GexfWriter::TRAVEL_TIME{"10", "travel_time", "string", "00:00:00"};

    const GexfWriter::GephiAttributeMeta GexfWriter::SAP_NUMBER{"11", "sap_number", "string", "unknown"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_RELATIVE{"12", "work_relative", "double", "0"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_ABSOLUTE_TIME{"13", "work_total_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_AVAILABLE_TIME{"14", "work_available_time", "string",
                                                                         "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_SERVICE_TIME{"15", "work_service_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_TRAVEL_TIME{"16", "work_travel_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_IDLE_TIME{"17", "work_idle_time", "string", "00:00:00"};
    const GexfWriter::GephiAttributeMeta GexfWriter::UTIL_VISITS_COUNT{"18", "work_visits_count", "long", "0"};

    void GexfWriter::Write(const boost::filesystem::path &file_path,
                           SolverWrapper &solver,
                           const operations_research::RoutingModel &model,
                           const operations_research::Assignment &solution,
                           const boost::optional<
                                   std::map<int,
                                           std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> >
                                   > > &activities) const {

        // TODO: output breaks

        static const auto VISIT_NODE = "visit";
        static const auto CARER_NODE = "carer";
        static const auto BREAK_NODE = "break";
        static const auto SERVICE_USER_NODE = "user";
        static const auto TRUE_VALUE = "true";
        operations_research::RoutingDimension const *time_dim = model.GetMutableDimension(
                rows::SolverWrapper::TIME_DIMENSION);

        GexfEnvironmentWrapper gexf;
        gexf.SetDescription(solver.GetDescription(model, solution));

        const auto central_location = solver.depot();
        gexf.SetDefaultValues(central_location);

        for (operations_research::RoutingModel::NodeIndex visit_index{1}; visit_index < model.nodes(); ++visit_index) {
            const auto visit_id = gexf.VisitId(visit_index);
            const auto &visit = solver.NodeToVisit(visit_index);

            gexf.AddNode(visit_id, (boost::format("visit %1%") % visit_index.value()).str());
            gexf.SetNodeValue(visit_id, ID, visit.id());
            gexf.SetNodeValue(visit_id, TYPE, VISIT_NODE);
            if (visit.location()) {
                gexf.SetNodeValue(visit_id, LATITUDE, visit.location().get().latitude());
                gexf.SetNodeValue(visit_id, LONGITUDE, visit.location().get().longitude());
            }
            if (solution.Value(model.NextVar(visit_index.value())) == visit_index.value()) {
                gexf.SetNodeValue(visit_id, DROPPED, TRUE_VALUE);
            }

            const auto start_time_sec = solution.Min(time_dim->CumulVar(model.NodeToIndex(visit_index)));
            gexf.SetNodeValue(visit_id,
                              START_TIME,
                              boost::posix_time::ptime{visit.datetime().date(),
                                                       boost::posix_time::seconds(start_time_sec)});
            gexf.SetNodeValue(visit_id, DURATION, visit.duration());
            gexf.SetNodeValue(visit_id, USER, visit.service_user().id());
            gexf.SetNodeValue(visit_id, CARER_COUNT, static_cast<size_t>(visit.carer_count()));
        }

        static const SolutionValidator validator{};
        operations_research::RoutingModel::NodeIndex next_user_node{0};
        std::unordered_map<rows::ServiceUser,
                operations_research::RoutingModel::NodeIndex> user_ids;
        for (const auto &service_user : solver.problem().service_users()) {
            const auto user_node = next_user_node++;
            const auto inserted = user_ids.emplace(service_user, user_node);
            DCHECK(inserted.second);

            const auto user_id = gexf.ServiceUserId(user_node);
            gexf.AddNode(user_id, (boost::format("user %1%") % user_node).str());
            gexf.SetNodeValue(user_id, ID, service_user.id());
            gexf.SetNodeValue(user_id, TYPE, SERVICE_USER_NODE);

            const auto &location = service_user.location();
            gexf.SetNodeValue(user_id, LONGITUDE, location.longitude());
            gexf.SetNodeValue(user_id, LATITUDE, location.latitude());
            gexf.SetNodeValue(user_id,
                              UTIL_VISITS_COUNT,
                              std::to_string(solver.User(service_user).visit_count()));

            auto visit_counter = 1;
            for (operations_research::RoutingModel::NodeIndex visit_index{1};
                 visit_index < model.nodes();
                 ++visit_index) {
                const auto &visit = solver.NodeToVisit(visit_index);
                if (visit.service_user() != service_user) {
                    continue;
                }

                const auto visit_id = gexf.VisitId(visit_index);
                const auto edge_id = gexf.EdgeId(user_id, visit_id, "uv_");
                gexf.AddEdge(edge_id,
                             user_id,
                             visit_id,
                             (boost::format("Visit %1% of %2%")
                              % visit_counter++
                              % service_user.id()).str());
            }
        }

        for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
            const auto &carer = solver.Carer(vehicle);
            const auto carer_id = gexf.CarerId(operations_research::RoutingModel::NodeIndex{vehicle});

            std::vector<rows::ScheduledVisit> route;

            gexf.AddNode(carer_id, (boost::format("carer %1%") % vehicle).str());
            gexf.SetNodeValue(carer_id, ID, carer.sap_number());
            gexf.SetNodeValue(carer_id, TYPE, CARER_NODE);
            gexf.SetNodeValue(carer_id, SAP_NUMBER, carer.sap_number());

            if (!model.IsVehicleUsed(solution, vehicle)) {
                gexf.SetNodeValue(carer_id, DROPPED, TRUE_VALUE);
                continue;
            }

            auto start_visit_index = model.Start(vehicle);
            DCHECK(!model.IsEnd(solution.Value(model.NextVar(start_visit_index))));
            do {
                const auto start_visit_node = model.IndexToNode(start_visit_index);
                if (start_visit_node != SolverWrapper::DEPOT) {
                    std::string start_visit_id = gexf.VisitId(start_visit_node);

                    const auto &calendar_visit = solver.NodeToVisit(start_visit_node);
                    route.emplace_back(ScheduledVisit::VisitType::UNKNOWN, carer, calendar_visit);

                    const auto carer_visit_edge_id = gexf.EdgeId(carer_id, start_visit_id, "c_");
                    gexf.AddEdge(carer_visit_edge_id,
                                 carer_id,
                                 start_visit_id,
                                 (boost::format("NodeToCarer %1% does visit %2%")
                                  % vehicle
                                  % start_visit_node).str());

                    if (!model.IsEnd(start_visit_index)) {
                        gexf.SetNodeValue(start_visit_id, ASSIGNED_CARER, carer.sap_number());
                    }

                    const auto &service_user = solver.User(solver.NodeToVisit(start_visit_node).service_user());
                    gexf.SetNodeValue(start_visit_id,
                                      SATISFACTION,
                                      std::to_string(service_user.Preference(carer)));

                    if (model.IsEnd(start_visit_index)) {
                        break;
                    }

                    const auto end_visit_index = solution.Value(model.NextVar(start_visit_index));
                    const auto end_visit_node = model.IndexToNode(end_visit_index);
                    if (end_visit_node != SolverWrapper::DEPOT) {
                        std::string end_visit_id = gexf.VisitId(end_visit_node);

                        const auto visit_visit_edge_id = gexf.EdgeId(start_visit_id, end_visit_id, "r_");
                        gexf.AddEdge(visit_visit_edge_id,
                                     start_visit_id,
                                     end_visit_id,
                                     (boost::format("Visit %1% after %2%")
                                      % start_visit_node
                                      % end_visit_node).str());

                        const auto travel_time = solver.Distance(start_visit_node, end_visit_node);
                        DCHECK_GE(travel_time, 0);
                        gexf.SetEdgeValue(visit_visit_edge_id,
                                          TRAVEL_TIME,
                                          boost::posix_time::seconds(travel_time));
                    }
                }

                start_visit_index = solution.Value(model.NextVar(start_visit_index));
            } while (!model.IsEnd(start_visit_index));

            if (activities) {
                const auto carer_node = operations_research::RoutingModel::NodeIndex{vehicle};
                operations_research::RoutingModel::NodeIndex break_node{1};
                for (const auto &local_activity : activities->at(vehicle)) {
                    if (local_activity->activity_type() != rows::RouteValidatorBase::ActivityType::Break) {
                        continue;
                    }

                    const auto break_id = gexf.BreakId(carer_node, break_node);
                    gexf.AddNode(break_id, (boost::format("break %1% carer %2%") % break_node % carer_node).str());
                    gexf.SetNodeValue(break_id, TYPE, BREAK_NODE);
                    gexf.SetNodeValue(break_id, ASSIGNED_CARER, carer.sap_number());
                    gexf.SetNodeValue(break_id, START_TIME, local_activity->period().begin());
                    gexf.SetNodeValue(break_id, DURATION, local_activity->duration());

                    ++break_node;
                }
            }

            if (route.empty()) {
                continue;
            }

            gexf.SetNodeValue(carer_id, UTIL_VISITS_COUNT, std::to_string(route.size()));
            const auto validation_result = validator.ValidateFull(vehicle, solution, model, solver);
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
        for (const auto &attr : {ID, TYPE, DROPPED, START_TIME, DURATION, ASSIGNED_CARER, USER,
                                 SATISFACTION,
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
                                                          const boost::posix_time::ptime &value) {
        SetNodeValue(node_id, attribute, boost::posix_time::to_simple_string(value));
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
                                                          std::size_t value) {
        SetNodeValue(node_id, attribute, std::to_string(value));
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

    std::string GexfWriter::GexfEnvironmentWrapper::BreakId(operations_research::RoutingModel::NodeIndex carer_index,
                                                            operations_research::RoutingModel::NodeIndex break_index) const {
        return (boost::format("c%1%_b%2%") % carer_index % break_index).str();
    }

    std::string GexfWriter::GexfEnvironmentWrapper::EdgeId(const std::string &from_id,
                                                           const std::string &to_id,
                                                           const std::string &prefix) const {
        return (boost::format("e%1%%2%%3%")
                % prefix
                % from_id
                % to_id).str();
    }

    std::string GexfWriter::GexfEnvironmentWrapper::ServiceUserId(
            operations_research::RoutingModel::NodeIndex service_user_id) const {
        return (boost::format("u%1%")
                % service_user_id).str();
    }

    void GexfWriter::GexfEnvironmentWrapper::SetDescription(std::string description) {
        env_ptr_->getMetaData().setDescription(description);
    }
}
