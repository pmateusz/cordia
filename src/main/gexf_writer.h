#ifndef ROWS_GEXFWRITER_H
#define ROWS_GEXFWRITER_H

#include <memory>
#include <string>
#include <utility>
#include <unordered_map>

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

#include <libgexf/gexf.h>
#include <libgexf/libgexf.h>

#include <boost/config.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time.hpp>
#include <boost/optional.hpp>

#include "location.h"
#include "solver_wrapper.h"

namespace rows {

    class SolverWrapper;

    class GexfWriter {
    public:

        struct GephiAttributeMeta {
            GephiAttributeMeta(std::string id, std::string name, std::string type, std::string default_value) noexcept
                    : Id(std::move(id)),
                      Name(std::move(name)),
                      Type(std::move(type)),
                      DefaultValue(std::move(default_value)) {}

            const std::string Id;
            const std::string Name;
            const std::string Type;
            const std::string DefaultValue;
        };

        static const GephiAttributeMeta ID;
        static const GephiAttributeMeta LONGITUDE;
        static const GephiAttributeMeta LATITUDE;
        static const GephiAttributeMeta START_TIME;
        static const GephiAttributeMeta DURATION;
        static const GephiAttributeMeta ASSIGNED_CARER;
        static const GephiAttributeMeta TYPE;
        static const GephiAttributeMeta SATISFACTION;
        static const GephiAttributeMeta USER;
        static const GephiAttributeMeta CARER_COUNT;

        static const GephiAttributeMeta TRAVEL_TIME;

        static const GephiAttributeMeta SAP_NUMBER;
        static const GephiAttributeMeta UTIL_RELATIVE;
        static const GephiAttributeMeta UTIL_ABSOLUTE_TIME;
        static const GephiAttributeMeta UTIL_AVAILABLE_TIME;
        static const GephiAttributeMeta UTIL_SERVICE_TIME;
        static const GephiAttributeMeta UTIL_TRAVEL_TIME;
        static const GephiAttributeMeta UTIL_IDLE_TIME;
        static const GephiAttributeMeta UTIL_VISITS_COUNT;
        static const GephiAttributeMeta DROPPED;

        void Write(const boost::filesystem::path &file_path,
                   SolverWrapper &solver,
                   const operations_research::RoutingIndexManager &index_manager,
                   const operations_research::RoutingModel &model,
                   const operations_research::Assignment &solution,
                   const boost::optional<
                           std::map<int, std::list<std::shared_ptr<RouteValidatorBase::FixedDurationActivity> > >
                   > &activities) const;

    private:
        class GexfEnvironmentWrapper {
        public:
            GexfEnvironmentWrapper();

            void SetDefaultValues(const rows::Location &location);

            std::string DepotId(operations_research::RoutingNodeIndex depot_index) const;

            std::string ServiceUserId(operations_research::RoutingNodeIndex service_user_id) const;

            std::string CarerId(operations_research::RoutingNodeIndex carer_index) const;

            std::string VisitId(operations_research::RoutingNodeIndex visit_index) const;

            std::string BreakId(operations_research::RoutingNodeIndex carer_index,
                                operations_research::RoutingNodeIndex break_node) const;

            std::string EdgeId(const std::string &from_id,
                               const std::string &to_id,
                               const std::string &prefix) const;

            void SetDescription(std::string description);

            void AddNode(const std::string &node_id,
                         const std::string &label);

            void SetNodeValue(const std::string &node_id,
                              const GephiAttributeMeta &attribute,
                              std::size_t value);

            void SetNodeValue(const std::string &node_id,
                              const GephiAttributeMeta &attribute,
                              const boost::posix_time::time_duration &value);

            void SetNodeValue(const std::string &node_id,
                              const GephiAttributeMeta &attribute,
                              const boost::posix_time::ptime &value);

            void SetNodeValue(const std::string &node_id,
                              const GephiAttributeMeta &attribute,
                              const osrm::util::FixedLongitude &value);

            void SetNodeValue(const std::string &node_id,
                              const GephiAttributeMeta &attribute,
                              const osrm::util::FixedLatitude &value);

            void SetNodeValue(const std::string &node_id,
                              const GephiAttributeMeta &attribute,
                              const std::string &value);

            void AddEdge(const std::string &edge_id,
                         const std::string &from_id,
                         const std::string &to_id,
                         const std::string &label);

            void SetEdgeValue(const std::string &edge_id,
                              const GephiAttributeMeta &attribute,
                              const boost::posix_time::time_duration &value);

            void Write(const boost::filesystem::path &file_path);

        private:
            std::unique_ptr<libgexf::GEXF> env_ptr_;
        };
    };
}


#endif //ROWS_GEXFWRITER_H
