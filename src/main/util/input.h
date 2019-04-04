#ifndef ROWS_INPUT_H
#define ROWS_INPUT_H

#include <string>
#include <memory>

#include "problem.h"
#include "printer.h"

#include <osrm/engine/engine_config.hpp>
#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <osrm/osrm.hpp>

namespace util {

    static const std::string JSON_FORMAT{"json"};
    static const std::string TEXT_FORMAT{"txt"};
    static const std::string LOG_FORMAT{"log"};

    rows::Problem LoadProblem(const std::string &problem_path, std::shared_ptr<rows::Printer> printer);

    rows::Problem LoadReducedProblem(const std::string &problem_path,
                                     const std::string &scheduling_date,
                                     std::shared_ptr<rows::Printer> printer);

    std::shared_ptr<rows::Printer> CreatePrinter(const std::string &format);

    bool ValidateConsoleFormat(const char *flagname, const std::string &value);

    osrm::EngineConfig CreateEngineConfig(const std::string &maps_file);
}


#endif //ROWS_INPUT_H
