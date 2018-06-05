#include <gflags/gflags.h>

#include <glog/logging.h>

#include <osrm/engine/engine_config.hpp>
#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <osrm/osrm.hpp>

#include <nlohmann/json.hpp>

#include "util/logging.h"
#include "util/error_code.h"
#include "util/validation.h"
#include "util/aplication_error.h"
#include "location_container.h"

DEFINE_string(maps, "../data/scotland-latest.osrm", "a file path to the map");
DEFINE_validator(maps, &util::file::Exists);

nlohmann::json route(nlohmann::json args, rows::LocationContainer &location_container) {
    static const rows::Location::JsonLoader LOCATION_LOADER{};

    const auto source_it = args.find("source");
    if (source_it == std::end(args)) {
        return {{"status",  "error"},
                {"message", "Key 'source' not found"}};
    }

    const auto destination_it = args.find("destination");
    if (destination_it == std::end(args)) {
        return {{"status",  "error"},
                {"message", "Key 'destination' not found"}};
    }

    const auto source = LOCATION_LOADER.Load(*source_it);
    const auto destination = LOCATION_LOADER.Load(*destination_it);
    return {};
}

int main(int argc, char **argv) {
    util::SetupLogging(argv[0]);
    gflags::SetVersionString("1.0.0");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling\n"
                            "Example: rows-routing-server"
                            " --maps=./data/scotland-latest.osrm");

    static const auto REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    osrm::EngineConfig routing_config;
    routing_config.storage_config = osrm::StorageConfig(FLAGS_maps);
    routing_config.use_shared_memory = false;
    routing_config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    if (!routing_config.IsValid()) {
        throw util::ApplicationError("Invalid Open Street Map engine configuration", util::ErrorCode::ERROR);
    }

    rows::LocationContainer location_container{routing_config};

    std::string current_line;
    while (std::getline(std::cin, current_line)) {
        if (current_line.empty()) { continue; }
        try {
            const auto args = nlohmann::json::parse(current_line);
            const auto command_it = args.find("command");
            if (command_it == std::cend(args)) {
                LOG(ERROR) << "Key 'command' not found";
                continue;
            }

            const auto command = command_it->get<std::string>();
            if (command == "shutdown") {
                // { 'command': 'shutdown' }
                std::cout << "{'status':'ok'}" << std::endl;
                return 0;
            } else if (command == "route") {
                // { "command": "route", "source": {"latitude": 55.8619711, "longitude": -4.2474694}, "destination": {"latitude": 55.862913, "longitude": -4.2599106} }
                std::cout << route(args, location_container);
            }
        } catch (const std::invalid_argument &ex) {
            LOG(ERROR) << ex.what();
        } catch (const std::domain_error &ex) {
            LOG(ERROR) << ex.what();
        }
    }

    return 0;
}
