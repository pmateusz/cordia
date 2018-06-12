#include "solution_dumper.h"

#include <chrono>

#include <boost/format.hpp>

#include <glog/logging.h>

#include "util/routing.h"

rows::SolutionDumper::SolutionDumper(boost::filesystem::path export_directory,
                                     std::string file_name_pattern,
                                     const operations_research::RoutingModel &model)
        : ProgressMonitor(model),
          export_directory_(std::move(export_directory)),
          file_name_pattern_(std::move(file_name_pattern)) {}

bool rows::SolutionDumper::AtSolution() {
    const auto dropped_visits = util::GetDroppedVisitCount(model());
    const auto solutions = solver()->solutions();

    if (dropped_visits == 10) {
        const auto solution_file
                = export_directory_ / (boost::format(file_name_pattern_) % dropped_visits).str();
        const auto solution_saved = model().WriteAssignment(solution_file.string());
        if (!solution_saved) {
            LOG(WARNING) << "Failed to save the solution";
        }
    }

    return true;
}
