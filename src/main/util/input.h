#ifndef ROWS_INPUT_H
#define ROWS_INPUT_H

#include <string>
#include <memory>
#include <regex>

#include "problem.h"
#include "printer.h"
#include "solution.h"

#include <osrm/engine/engine_config.hpp>
#include <osrm/coordinate.hpp>
#include <osrm/engine_config.hpp>
#include <osrm/json_container.hpp>
#include <osrm/storage_config.hpp>
#include <osrm/osrm.hpp>

#include <boost/config.hpp>
#include <boost/date_time.hpp>

namespace util {

    static const std::string JSON_FORMAT{"json"};
    static const std::string TEXT_FORMAT{"txt"};
    static const std::string LOG_FORMAT{"log"};

    namespace string {

        void Strip(std::string &text);

        void ToLower(std::string &text);
    }

    rows::Problem LoadProblem(const std::string &problem_path, std::shared_ptr<rows::Printer> printer);

    rows::Problem LoadReducedProblem(const std::string &problem_path,
                                     const std::string &scheduling_date,
                                     std::shared_ptr<rows::Printer> printer);

    rows::Solution LoadSolution(const std::string &solution_path,
                                const rows::Problem &problem,
                                const boost::posix_time::time_duration &visit_time_window);

    std::shared_ptr<rows::Printer> CreatePrinter(const std::string &format);

    bool ValidateConsoleFormat(const char *flagname, const std::string &value);

    boost::posix_time::time_duration GetTimeDurationOrDefault(const std::string &text,
                                                              boost::posix_time::time_duration default_value);

    osrm::EngineConfig CreateEngineConfig(const std::string &maps_file);

    template<typename CancellableType>
    void ChatBot(CancellableType &cancellation_token) {
        std::regex non_printable_character_pattern{"[\\W]"};

        std::string line;
        while (true) {
            std::getline(std::cin, line);
            util::string::Strip(line);
            util::string::ToLower(line);

            if (!line.empty() && line == "stop") {
                cancellation_token.Cancel();
                break;
            }

            line.clear();
        }
    }
}


#endif //ROWS_INPUT_H
