#include "break.h"

rows::Break::Break(rows::Carer carer,
                   boost::posix_time::ptime datetime,
                   boost::posix_time::ptime::time_duration_type duration)
        : carer_{std::move(carer)},
          datetime_{datetime},
          duration_{std::move(duration)} {}

bool rows::Break::operator==(const rows::Break &other) const {
    return carer_ == other.carer_
           && datetime_ == other.datetime_
           && duration_ == other.duration_;
}

std::ostream &operator<<(std::ostream &out, const rows::Break &break_element) {
    out << "Break ["
        << "carer=" << break_element.carer()
        << ", start=" << break_element.datetime()
        << ", duration=" << break_element.duration() << "]";
    return out;
}
