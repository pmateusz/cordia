#ifndef ROWS_GEXFWRITER_H
#define ROWS_GEXFWRITER_H

#include <string>
#include <utility>

#include <boost/filesystem.hpp>

#include <ortools/constraint_solver/constraint_solver.h>
#include <ortools/constraint_solver/routing.h>

#include "solver_wrapper.h"

namespace rows {

    class GexfWriter {
    public:

        struct GephiAttributeMeta {
            GephiAttributeMeta(std::string id, std::string name, std::string type, std::string default_value)
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
        static const GephiAttributeMeta TRAVEL_TIME;

        void Write(const boost::filesystem::path &file_path,
                   SolverWrapper &solver,
                   const operations_research::RoutingModel &model,
                   const operations_research::Assignment &solution) const;
    };
}


#endif //ROWS_GEXFWRITER_H
