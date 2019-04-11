#include "input.h"

#include <unordered_set>

#include <boost/config.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/date_time.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include "util/error_code.h"
#include "util/validation.h"
#include "calendar_visit.h"

rows::Problem util::LoadProblem(const std::string &problem_path, std::shared_ptr<rows::Printer> printer) {
    boost::filesystem::path problem_file(boost::filesystem::canonical(problem_path));
    std::ifstream problem_stream;
    problem_stream.open(problem_file.c_str());
    if (!problem_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     util::ErrorCode::ERROR);
    }

    nlohmann::json problem_json;
    try {
        problem_stream >> problem_json;
    } catch (...) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % problem_file).str(),
                                     boost::current_exception_diagnostic_information(),
                                     util::ErrorCode::ERROR);
    }

    try {
        rows::Problem::JsonLoader json_loader;
        const auto initial_problem = json_loader.Load(problem_json);

        std::vector<rows::CalendarVisit> visits_to_use;
        for (const auto &visit : initial_problem.visits()) {
            if (visit.duration().total_seconds() > 0) {
                visits_to_use.push_back(visit);
            }
        }

        LOG_IF(WARNING, visits_to_use.size() != initial_problem.visits().size()) << "Removed "
                                                                                 << initial_problem.visits().size() -
                                                                                    visits_to_use.size() << " visits ";

        return rows::Problem(std::move(visits_to_use), initial_problem.carers(), initial_problem.service_users());
    } catch (const std::domain_error &ex) {
        throw util::ApplicationError(
                (boost::format("Failed to parse the file %1% due to error: '%2%'") % problem_file %
                 ex.what()).str(),
                util::ErrorCode::ERROR);
    }
}

rows::Problem util::LoadReducedProblem(const std::string &problem_path,
                                       const std::string &scheduling_date_string,
                                       std::shared_ptr<rows::Printer> printer) {
    const auto problem = LoadProblem(problem_path, printer);

    const std::pair<boost::posix_time::ptime, boost::posix_time::ptime> timespan_pair = problem.Timespan();
    const auto begin_date = timespan_pair.first.date();
    const auto end_date = timespan_pair.second.date();

    if (scheduling_date_string.empty()) {
        if (begin_date < end_date) {
            printer->operator<<(
                    (boost::format("Problem contains records from several days."
                                   " The computed solution will be reduced to a single day: '%1%'")
                     % begin_date).str());

            return problem.Trim(timespan_pair.first, boost::posix_time::hours(24));
        }

        return problem;
    }

    const boost::posix_time::ptime scheduling_time{boost::gregorian::from_simple_string(scheduling_date_string)};
    const auto scheduling_date = scheduling_time.date();
    if (begin_date == end_date && begin_date == scheduling_date) {
        return problem;
    } else if (begin_date <= scheduling_date && scheduling_date <= end_date) {
        return problem.Trim(scheduling_time, boost::posix_time::hours(24));
    } else {
        throw util::ApplicationError(
                (boost::format("Scheduling day '%1%' does not fin into the interval ['%2%','%3%']")
                 % scheduling_date
                 % timespan_pair.first
                 % timespan_pair.second).str(),
                util::ErrorCode::ERROR);
    }
}

bool util::ValidateConsoleFormat(const char *flagname, const std::string &value) {
    std::string value_to_use{value};
    util::string::Strip(value_to_use);
    util::string::ToLower(value_to_use);
    return value_to_use == JSON_FORMAT || value_to_use == TEXT_FORMAT || value_to_use == LOG_FORMAT;
}

std::shared_ptr<rows::Printer> util::CreatePrinter(const std::string &format) {
    auto format_to_use = format;
    util::string::Strip(format_to_use);
    util::string::ToLower(format_to_use);
    if (format_to_use == JSON_FORMAT) {
        return std::make_shared<rows::JsonPrinter>();
    }

    if (format_to_use == TEXT_FORMAT) {
        return std::make_shared<rows::ConsolePrinter>();
    }

    if (format_to_use == LOG_FORMAT) {
        return std::make_shared<rows::LogPrinter>();
    }

    throw util::ApplicationError("Unknown console format.", util::ErrorCode::ERROR);
}

osrm::EngineConfig util::CreateEngineConfig(const std::string &maps_file) {
    osrm::EngineConfig config;
    config.storage_config = osrm::StorageConfig(maps_file);
    config.use_shared_memory = false;
    config.algorithm = osrm::EngineConfig::Algorithm::MLD;

    if (!config.IsValid()) {
        throw util::ApplicationError("Invalid Open Street Map engine configuration", util::ErrorCode::ERROR);
    }

    return config;
}

rows::Solution util::LoadSolution(const std::string &solution_path,
                                  const rows::Problem &problem,
                                  const boost::posix_time::time_duration &visit_time_window) {
    boost::filesystem::path solution_file(boost::filesystem::canonical(solution_path));
    std::ifstream solution_stream;
    solution_stream.open(solution_file.c_str());
    if (!solution_stream.is_open()) {
        throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                     util::ErrorCode::ERROR);
    }

    rows::Solution original_solution;
    const std::string file_extension{solution_file.extension().string()};
    if (file_extension == ".json") {
        nlohmann::json solution_json;
        try {
            solution_stream >> solution_json;
        } catch (...) {
            throw util::ApplicationError((boost::format("Failed to open the file: %1%") % solution_file).str(),
                                         boost::current_exception_diagnostic_information(),
                                         util::ErrorCode::ERROR);
        }


        try {
            rows::Solution::JsonLoader json_loader;
            original_solution = json_loader.Load(solution_json);
        } catch (const std::domain_error &ex) {
            throw util::ApplicationError(
                    (boost::format("Failed to parse the file '%1%' due to error: '%2%'") % solution_file %
                     ex.what()).str(),
                    util::ErrorCode::ERROR);
        }
    } else if (file_extension == ".gexf") {
        rows::Solution::XmlLoader xml_loader;
        original_solution = xml_loader.Load(solution_file.string());
    } else {
        throw util::ApplicationError(
                (boost::format("Unknown file format: '%1%'. Use 'json' or 'gexf' format instead.")
                 % file_extension).str(), util::ErrorCode::ERROR);
    }

    const auto time_span = problem.Timespan();
    return original_solution.Trim(time_span.first, time_span.second - time_span.first + visit_time_window);
}
