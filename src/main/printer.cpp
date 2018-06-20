#include <iostream>

#include <nlohmann/json.hpp>

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
                                         std::string area,
                                         boost::gregorian::date date,
                                         boost::posix_time::time_duration visit_time_window,
                                         boost::posix_time::time_duration break_time_window,
                                         boost::posix_time::time_duration shift_adjustment)
            : Carers{carers},
              Visits{visits},
              Area{std::move(area)},
              Date{date},
              VisitTimeWindow{std::move(visit_time_window)},
              BreakTimeWindow{std::move(break_time_window)},
              ShiftAdjustment{std::move(shift_adjustment)} {}

    void to_json(nlohmann::json &json, const ProblemDefinition &problem_definition) {
        json = nlohmann::json{
                {"carers",             problem_definition.Carers},
                {"visits",             problem_definition.Visits},
                {"area",               problem_definition.Area},
                {"date",               boost::gregorian::to_simple_string(problem_definition.Date)},
                {"visit_time_windows", boost::posix_time::to_simple_string(problem_definition.VisitTimeWindow)},
                {"break_time_windows", boost::posix_time::to_simple_string(problem_definition.BreakTimeWindow)},
                {"shift_adjustment",   boost::posix_time::to_simple_string(problem_definition.ShiftAdjustment)}
        };
    }

    void to_json(nlohmann::json &json, const TracingEvent &tracing_event) {
        json = nlohmann::json{
                {"type",    to_string(tracing_event.Type)},
                {"comment", tracing_event.Comment}
        };
    }

    void to_json(nlohmann::json &json, const ProgressStep &progress_step) {
        json = nlohmann::json{
                {"cost",           progress_step.Cost},
                {"dropped_visits", progress_step.DroppedVisits},
                {"solutions",      progress_step.Solutions},
                {"branches",       progress_step.Branches},
                {"memory_usage",   progress_step.MemoryUsage},
                {"wall_time",      boost::posix_time::to_simple_string(progress_step.WallTime)}
        };
    }

    Printer &Printer::operator<<(const std::string &text) {
        std::cout << text << std::endl;
        return *this;
    }

    Printer &ConsolePrinter::operator<<(const ProblemDefinition &problem_definition) {
        Printer::operator<<((boost::format(
                "Carers | Visits | Area | Date | Visit Time Window | Break Time Window | Shift Adjustment \n"
                "%6d | %6d | %11s | %11s | %14s | %14s | %14s")
                             % problem_definition.Carers
                             % problem_definition.Visits
                             % problem_definition.Area
                             % problem_definition.Date
                             % problem_definition.VisitTimeWindow
                             % problem_definition.BreakTimeWindow
                             % problem_definition.ShiftAdjustment).str());
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

    Printer &ConsolePrinter::operator<<(const TracingEvent &trace_event) {
        return *this;
    }

    Printer &JsonPrinter::operator<<(const std::string &text) {
        Printer::operator<<(nlohmann::json{
                {"type",    "message"},
                {"content", text}}.dump());
        return *this;
    }

    Printer &JsonPrinter::operator<<(const ProblemDefinition &problem_definition) {
        Printer::operator<<(nlohmann::json{
                {"type",    "problem_definition"},
                {"content", problem_definition}
        }.dump());
        return *this;
    }

    Printer &JsonPrinter::operator<<(const ProgressStep &progress_step) {
        Printer::operator<<(nlohmann::json{
                {"type",    "progress_step"},
                {"content", progress_step}
        }.dump());
        return *this;
    }

    Printer &JsonPrinter::operator<<(const TracingEvent &trace_event) {
        Printer::operator<<(nlohmann::json{
                {"type",    "tracing_event"},
                {"content", trace_event}
        }.dump());
        return *this;
    }

    TracingEvent::TracingEvent(TracingEventType type, std::string comment)
            : Type(type),
              Comment(std::move(comment)) {}

    std::string to_string(const TracingEventType &value) {
        static const std::string UNKNOWN_NAME{"unknown"};
        static const std::string STARTED_NAME{"started"};
        static const std::string FINISHED_NAME{"finished"};

        switch (value) {
            case TracingEventType::Unknown:
                return UNKNOWN_NAME;
            case TracingEventType::Started:
                return STARTED_NAME;
            case TracingEventType::Finished:
                return FINISHED_NAME;
            default:
                std::string error_msg = "Conversion to std::string not defined for value=" + static_cast<int>(value);
                throw std::invalid_argument(std::move(error_msg));
        }
    }

    Printer &LogPrinter::operator<<(const std::string &text) {
        LOG(INFO) << text;
        return *this;
    }

    Printer &LogPrinter::operator<<(const ProblemDefinition &problem_definition) {
        LogPrinter::operator<<(static_cast<nlohmann::json>(problem_definition).dump());
        return *this;
    }

    Printer &LogPrinter::operator<<(const TracingEvent &trace_event) {
        LogPrinter::operator<<(static_cast<nlohmann::json>(trace_event).dump());
        return *this;
    }

    Printer &LogPrinter::operator<<(const ProgressStep &progress_step) {
        LogPrinter::operator<<(static_cast<nlohmann::json>(progress_step).dump());
        return *this;
    }
}
