#include "diary.h"

#include <vector>
#include <string>

#include <boost/date_time.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/join.hpp>

namespace rows {

    Diary::Diary()
            : date_(boost::date_time::not_a_date_time) {}

    Diary::Diary(boost::gregorian::date date, std::vector<rows::Event> events)
            : date_(date),
              events_(std::move(events)) {}

    Diary::Diary(const Diary &other)
            : date_(other.date_),
              events_(other.events_) {}

    Diary::Diary(Diary &&other) noexcept
            : date_(other.date_),
              events_(std::move(other.events_)) {}

    Diary &Diary::operator=(const Diary &other) {
        date_ = other.date_;
        events_ = other.events_;
        return *this;
    }

    Diary &Diary::operator=(Diary &&other) noexcept {
        date_ = other.date_;
        events_ = std::move(other.events_);
        return *this;
    }

    bool Diary::operator==(const Diary &other) const {
        return date_ == other.date_
               && events_ == other.events_;
    }

    bool Diary::operator!=(const Diary &other) const {
        return !operator==(other);
    }

    boost::posix_time::ptime::date_type Diary::date() const {
        return date_;
    }

    std::vector<rows::Event> Diary::Breaks() const {
        if (events_.empty()) {
            return {rows::Event(boost::posix_time::time_period(boost::posix_time::ptime(date_),
                                                               boost::posix_time::hours(24)))};
        }

        auto breaks = std::vector<Event>();
        breaks.emplace_back(
                boost::posix_time::time_period(boost::posix_time::ptime(date_), events_.front().begin().time_of_day()));

        const auto events_size = events_.size();
        for (int index = 1; index < events_size; ++index) {
            const auto last_event_finish = events_[index - 1].end().time_of_day();
            const auto current_event_start = events_[index].begin().time_of_day();
            breaks.emplace_back(boost::posix_time::time_period(boost::posix_time::ptime(date_, last_event_finish),
                                                               current_event_start - last_event_finish));
        }

        breaks.emplace_back(
                boost::posix_time::time_period(boost::posix_time::ptime(date_, events_.back().end().time_of_day()),
                                               boost::posix_time::hours(24) - events_.back().end().time_of_day()));

        return breaks;
    }

    const std::vector<rows::Event> &Diary::events() const {
        return events_;
    }

    boost::posix_time::ptime::time_duration_type Diary::begin_time() const {
        if (events_.empty()) {
            return {};
        }

        return events_.front().begin().time_of_day();
    }

    boost::posix_time::ptime::time_duration_type Diary::end_time() const {
        if (events_.empty()) {
            return {};
        }

        return events_.back().end().time_of_day();
    }

    std::ostream &operator<<(std::ostream &out, const Diary &object) {
        std::vector<std::string> events;
        for (const auto &event : object.events_) {
            std::stringstream stream;
            stream << event;
            events.emplace_back(stream.str());
        }

        out << boost::format("(%1%, [%2%])")
               % object.date_
               % boost::algorithm::join(events, ", ");
        return out;
    }
}
