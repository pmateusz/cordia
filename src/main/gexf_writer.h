#ifndef ROWS_GEXFWRITER_H
#define ROWS_GEXFWRITER_H

#include <memory>
#include <string>
#include <utility>
#include <unordered_map>

#include <libgexf/gexf.h>
#include <libgexf/libgexf.h>

#include <boost/filesystem.hpp>
#include <boost/date_time.hpp>

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>
#include "location.h"

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
                   const operations_research::RoutingModel &model,
                   const operations_research::Assignment &solution) const;

    private:
        class GexfEnvironmentWrapper {
        public:
            GexfEnvironmentWrapper();

            void SetDefaultValues(const rows::Location &location);

            void AddNode(operations_research::RoutingModel::NodeIndex index);

            void SetNodeLabel(operations_research::RoutingModel::NodeIndex index,
                              const std::string &value);

            void SetNodeValue(operations_research::RoutingModel::NodeIndex index,
                              const GephiAttributeMeta &attribute,
                              boost::posix_time::time_duration value);

            void SetNodeValue(operations_research::RoutingModel::NodeIndex index,
                              const GephiAttributeMeta &attribute,
                              std::string value);

            void SetNodeValue(operations_research::RoutingModel::NodeIndex index,
                              const GephiAttributeMeta &attribute,
                              osrm::util::FixedLongitude value);

            void SetNodeValue(operations_research::RoutingModel::NodeIndex index,
                              const GephiAttributeMeta &attribute,
                              osrm::util::FixedLatitude value);

            void AddEdge(operations_research::RoutingModel::NodeIndex from,
                         operations_research::RoutingModel::NodeIndex to);

            void SetEdgeValue(operations_research::RoutingModel::NodeIndex from,
                              operations_research::RoutingModel::NodeIndex to,
                              const GephiAttributeMeta &attribute,
                              const boost::posix_time::time_duration &value);

            void Write(const boost::filesystem::path &file_path);

        private:
            std::unique_ptr<libgexf::GEXF> env_ptr_;

            long next_node_id_;
            std::unordered_map<operations_research::RoutingModel::NodeIndex, std::string> node_ids_;

            long next_edge_id_;
            std::unordered_map<
                    std::pair<operations_research::RoutingModel::NodeIndex,
                            operations_research::RoutingModel::NodeIndex>,
                    std::string> edge_ids_;
        };
    };
}


#endif //ROWS_GEXFWRITER_H
