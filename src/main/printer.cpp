#include <iostream>

#include <boost/format.hpp>

#include "printer.h"

static std::string HumanReadableSize(long bytes) {
    static const auto UNIT = 1024;
    if (bytes < UNIT) {
        return std::to_string(bytes) + " B";
    }

    auto exp = static_cast<int>(std::log(bytes) / std::log(UNIT));
    auto prefix = "KMGTPE"[exp - 1];
    return (boost::format("%.1f %sB") % (bytes / std::pow(UNIT, exp)) % prefix).str();
}

namespace rows {

    ProgressStep::ProgressStep(double cost,
                               std::size_t dropped_visits,
                               boost::posix_time::time_duration wall_time,
                               std::size_t branches,
                               std::size_t solutions,
                               std::size_t memory_usage)
            : Cost{cost},
              DroppedVisits{dropped_visits},
              WallTime{std::move(wall_time)},
              Branches{branches},
              Solutions{solutions},
              MemoryUsage{memory_usage} {}

    ProblemDefinition::ProblemDefinition(int carers,
                                         int visits,
                                         boost::posix_time::time_duration time_window,
                                         int covered_visits)
            : Carers{carers},
              Visits{visits},
              TimeWindow{std::move(time_window)},
              CoveredVisits{covered_visits} {}

    Printer &Printer::operator<<(const std::string &text) {
        std::cout << text << std::endl;
        return *this;
    }

    Printer &ConsolePrinter::operator<<(const ProblemDefinition &problem_definition) {
        Printer::operator<<((boost::format("Carers | Visits | Time Window | Covered Visits | Dropped Visits\n"
                                                   "%6d | %6d | %11s | %14s | %5s")
                             % problem_definition.Carers
                             % problem_definition.Visits
                             % problem_definition.TimeWindow
                             % problem_definition.CoveredVisits
                             % (problem_definition.Visits - problem_definition.CoveredVisits)).str());
        return *this;
    }

    Printer &ConsolePrinter::operator<<(const ProgressStep &progress_step) {
        if (header_printed_) { ;
            Printer::operator<<((boost::format("%10e | %14d | %9s | %8d | %9d | %12s")
                                 % progress_step.Cost
                                 % progress_step.DroppedVisits
                                 % progress_step.WallTime
                                 % progress_step.Branches
                                 % progress_step.Solutions
                                 % HumanReadableSize(progress_step.MemoryUsage)).str());
            return *this;
        }

        header_printed_ = true;
        Printer::operator<<((boost::format("%12s | %10s | %5s | %5s | %5s | %12s")
                             % "Cost"
                             % "Dropped Visits"
                             % "Wall Time"
                             % "Branches"
                             % "Solutions"
                             % "Memory Usage").str());
        return this->operator<<(progress_step);
    }

    Printer &JsonPrinter::operator<<(const ProblemDefinition &problem_definition) {
        return *this;
    }

    Printer &JsonPrinter::operator<<(const ProgressStep &progress_step) {
        return *this;
    }
}
