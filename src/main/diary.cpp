#include "diary.h"

#include <vector>
#include <string>

#include <glog/logging.h>

#include <boost/date_time.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string/join.hpp>

namespace rows {

    Diary::Diary()
            : date_(boost::date_time::not_a_date_time),
              events_() {}

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

    const boost::gregorian::date &Diary::date() const {
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

    boost::posix_time::ptime::time_duration_type Diary::duration() const {
        boost::posix_time::ptime::time_duration_type duration;
        for (const auto &event : events_) {
            duration += event.duration();
        }
        return duration;
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

    Diary Diary::Intersect(const Diary &other) const {
        DCHECK_EQ(date_, other.date_);

        auto left_events_it = std::begin(events_);
        auto right_events_it = std::begin(other.events_);
        const auto left_events_it_end = std::end(events_);
        const auto right_events_it_end = std::end(other.events_);

        std::vector<rows::Event> overlapping_events;
        while ((left_events_it != left_events_it_end) && (right_events_it != right_events_it_end)) {
            if (right_events_it->end() > left_events_it->end()) {
                // left finishes faster

                if (left_events_it->end() > right_events_it->begin()) {
                    const auto begin = std::max(left_events_it->begin(), right_events_it->begin());
                    if (begin < left_events_it->end()) {
                        overlapping_events.emplace_back(boost::posix_time::time_period(begin, left_events_it->end()));
                    }
                }

                ++left_events_it;
            } else if (left_events_it->end() > right_events_it->end()) {
                // right finishes faster

                if (right_events_it->end() > left_events_it->begin()) {
                    const auto begin = std::max(left_events_it->begin(), right_events_it->begin());
                    if (begin < right_events_it->end()) {
                        overlapping_events.emplace_back(boost::posix_time::time_period(begin, right_events_it->end()));
                    }
                }

                ++right_events_it;
            } else {
                // both finish at the same time
                const auto begin = std::max(left_events_it->begin(), right_events_it->begin());
                if (begin < left_events_it->end()) {
                    overlapping_events.emplace_back(boost::posix_time::time_period(begin, left_events_it->end()));
                }

                ++left_events_it;
                ++right_events_it;
            }
        }

        return {date_, overlapping_events};
    }

    bool Diary::IsAvailable(const boost::posix_time::ptime date_time,
                            const boost::posix_time::time_duration adjustment) const {
        for (const auto &event : events_) {
            if (event.Contains(date_time)) {
                return true;
            }
        }

        if (adjustment.total_seconds() > 0 && !events_.empty()) {
            return ((date_time < events_.front().begin()) && ((events_.front().begin() - date_time) <= adjustment))
                   || ((date_time > events_.back().end()) && ((date_time - events_.back().end()) <= adjustment));
        }

        return false;
    }
}
