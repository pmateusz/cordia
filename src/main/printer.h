#ifndef ROWS_PRINTER_H
#define ROWS_PRINTER_H

#include <string>

#include <boost/date_time.hpp>
#include <glog/logging.h>
#include <nlohmann/json.hpp>

namespace rows {

    struct ProblemDefinition {
        ProblemDefinition(int carers,
                          int visits,
                          std::string area,
                          boost::gregorian::date date,
                          boost::posix_time::time_duration visit_time_window,
                          boost::posix_time::time_duration break_time_window,
                          boost::posix_time::time_duration shift_adjustment);

        int Carers;
        int Visits;
        std::string Area;
        boost::gregorian::date Date;
        boost::posix_time::time_duration VisitTimeWindow;
        boost::posix_time::time_duration BreakTimeWindow;
        boost::posix_time::time_duration ShiftAdjustment;
    };

    void to_json(nlohmann::json &json, const ProblemDefinition &problem_definition);

    enum class TracingEventType {
        Unknown,
        Started,
        Finished
    };

    std::string to_string(const TracingEventType &value);

    struct TracingEvent {
        TracingEvent(TracingEventType type, std::string comment);

        TracingEventType Type;
        std::string Comment;
    };

    void to_json(nlohmann::json &json, const TracingEvent &tracing_event);

    struct ProgressStep {
        ProgressStep(double cost,
                     std::size_t dropped_visits,
                     boost::posix_time::time_duration wall_time,
                     std::size_t branches,
                     std::size_t solutions,
                     std::size_t memory_usage);

        double Cost;
        std::size_t DroppedVisits;
        boost::posix_time::time_duration WallTime;
        std::size_t Branches;
        std::size_t Solutions;
        std::size_t MemoryUsage;
    };

    void to_json(nlohmann::json &json, const ProgressStep &progress_step);

    class Printer {
    public:
        virtual ~Printer() = default;

        virtual Printer &operator<<(const std::string &text);

        virtual Printer &operator<<(const ProblemDefinition &problem_definition) = 0;

        virtual Printer &operator<<(const TracingEvent &trace_event) = 0;

        virtual Printer &operator<<(const ProgressStep &progress_step) = 0;
    };

    class ConsolePrinter : public Printer {
    public:
        ~ConsolePrinter() override = default;

        Printer &operator<<(const ProblemDefinition &problem_definition) override;

        Printer &operator<<(const TracingEvent &trace_event) override;

        Printer &operator<<(const ProgressStep &progress_step) override;

    private:
        bool header_printed_{false};
    };

    class JsonPrinter : public Printer {
    public:
        ~JsonPrinter() override = default;

        Printer &operator<<(const std::string &text) override;

        Printer &operator<<(const ProblemDefinition &problem_definition) override;

        Printer &operator<<(const TracingEvent &trace_event) override;

        Printer &operator<<(const ProgressStep &progress_step) override;
    };

    class LogPrinter : public Printer {
    public:
        ~LogPrinter() override = default;

        Printer &operator<<(const std::string &text) override;

        Printer &operator<<(const ProblemDefinition &problem_definition) override;

        Printer &operator<<(const TracingEvent &trace_event) override;

        Printer &operator<<(const ProgressStep &progress_step) override;
    };
}


#endif //ROWS_PRINTER_H
