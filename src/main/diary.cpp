#include "diary.h"

#include <vector>
#include <string>

#include <boost/format.hpp>
#include <boost/algorithm/string/join.hpp>

namespace rows {

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

    boost::gregorian::date Diary::date() const {
        return date_;
    }

    const std::vector<rows::Event> &Diary::events() const {
        return events_;
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
