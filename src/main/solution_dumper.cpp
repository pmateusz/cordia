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

//    if (dropped_visits <= 72) {
//        const auto time_point_now = std::chrono::system_clock::now();
//        std::time_t time = std::chrono::system_clock::to_time_t(time_point_now);
//        const auto tm = std::gmtime(&time);
//        const auto solution_file
//                = export_directory_ / (boost::format(file_name_pattern_)
//                                       % (boost::format("%1%_%2%_%3%")
//                                          % dropped_visits
//                                          % tm->tm_hour
//                                          % tm->tm_min)).str();
//        LOG(INFO) << "Dumping assignment: " << solution_file;
//
//        const auto solution_saved = model().WriteAssignment(solution_file.string());
//        if (!solution_saved) {
//            LOG(WARNING) << "Failed to save the solution";
//        }
//    }

    return true;
}
