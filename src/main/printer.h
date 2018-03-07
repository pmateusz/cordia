#ifndef ROWS_PRINTER_H
#define ROWS_PRINTER_H

#include <string>

#include <boost/date_time.hpp>
#include <glog/logging.h>

namespace rows {

    struct ProblemDefinition {
        ProblemDefinition(int carers,
                          int visits,
                          boost::posix_time::time_duration time_window,
                          int covered_visits);

        int Carers;
        int Visits;
        const boost::posix_time::time_duration TimeWindow;
        int CoveredVisits;
    };

    struct ProgressStep {
        ProgressStep(double cost,
                     std::size_t dropped_visits,
                     boost::posix_time::time_duration wall_time,
                     std::size_t branches,
                     std::size_t solutions,
                     std::size_t memory_usage);

        const double Cost;
        const std::size_t DroppedVisits;
        const boost::posix_time::time_duration WallTime;
        const std::size_t Branches;
        const std::size_t Solutions;
        const std::size_t MemoryUsage;
    };

    class Printer {
    public:
        virtual ~Printer() = default;

        virtual Printer &operator<<(const std::string &text);

        virtual Printer &operator<<(const ProblemDefinition &problem_definition) = 0;

        virtual Printer &operator<<(const ProgressStep &progress_step) = 0;
    };

    class ConsolePrinter : public Printer {
    public:
        ~ConsolePrinter() override = default;

        Printer &operator<<(const ProblemDefinition &problem_definition) override;

        Printer &operator<<(const ProgressStep &progress_step) override;

    private:
        bool header_printed_{false};
    };

    class JsonPrinter : public Printer {
    public:
        ~JsonPrinter() override = default;

        Printer &operator<<(const std::string &text) override;

        Printer &operator<<(const ProblemDefinition &problem_definition) override;

        Printer &operator<<(const ProgressStep &progress_step) override;
    };
}


#endif //ROWS_PRINTER_H
