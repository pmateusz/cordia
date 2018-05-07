#ifndef ROWS_SOLUTION_CATCHER_H
#define ROWS_SOLUTION_CATCHER_H

#include <string>

#include <boost/filesystem/path.hpp>

#include "progress_monitor.h"


namespace rows {

    class SolutionDumper : public rows::ProgressMonitor {
    public:
        SolutionDumper(boost::filesystem::path export_directory,
                       std::string file_name_pattern,
                       const operations_research::RoutingModel &model);

        bool AtSolution() override;

    private:
        boost::filesystem::path export_directory_;
        std::string file_name_pattern_;
    };
}


#endif //ROWS_SOLUTION_CATCHER_H
