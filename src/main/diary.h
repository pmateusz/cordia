#ifndef ROWS_DIARY_H
#define ROWS_DIARY_H

#include <vector>
#include <utility>
#include <ostream>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time.hpp>

#include "event.h"

namespace rows {

    class Diary {
    public:
        Diary();

        Diary(boost::gregorian::date date, std::vector<rows::Event> events);

        Diary(const Diary &other);

        Diary(Diary &&other) noexcept;

        Diary &operator=(const Diary &other);

        Diary &operator=(Diary &&other) noexcept;

        bool operator==(const Diary &other) const;

        bool operator!=(const Diary &other) const;

        boost::gregorian::date date() const;

        boost::posix_time::time_duration begin_time() const;

        boost::posix_time::time_duration end_time() const;

        const std::vector<rows::Event> &events() const;

        friend std::ostream &operator<<(std::ostream &out, const Diary &object);

    private:
        boost::gregorian::date date_;
        std::vector<rows::Event> events_;
    };
}


#endif //ROWS_DIARY_H
