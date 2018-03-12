#ifndef ROWS_SCHEDULED_VISIT_H
#define ROWS_SCHEDULED_VISIT_H

#include <ostream>

#include <glog/logging.h>
#include <boost/optional.hpp>

#include "calendar_visit.h"
#include "carer.h"
#include "location.h"

namespace rows {

    class ScheduledVisit {
    public:
        enum class VisitType {
            UNKNOWN, OK, CANCELLED, MOVED, INVALID
        };

        ScheduledVisit();

        ScheduledVisit(VisitType type, Carer carer, CalendarVisit calendar_visit);

        ScheduledVisit(VisitType type,
                       boost::optional<Carer> carer,
                       boost::posix_time::ptime datetime,
                       boost::posix_time::ptime::time_duration_type duration,
                       boost::optional<boost::posix_time::ptime> check_in,
                       boost::optional<boost::posix_time::ptime> check_out,
                       boost::optional<CalendarVisit> calendar_visit);

        ScheduledVisit(const ScheduledVisit &other);

        ScheduledVisit(ScheduledVisit &&other) noexcept;

        ScheduledVisit &operator=(const ScheduledVisit &other);

        ScheduledVisit &operator=(ScheduledVisit &&other) noexcept;

        friend std::ostream &operator<<(std::ostream &out, const ScheduledVisit &visit);

        class JsonLoader {
        public:
            template<typename JsonType>
            ScheduledVisit Load(const JsonType &document) const;
        };

        const boost::optional<Carer> &carer() const;

        boost::optional<Carer> &carer();

        boost::optional<ServiceUser> service_user() const;

        boost::optional<Address> address() const;

        void address(const Address &address);

        const boost::posix_time::ptime &datetime() const;

        int carer_count() const;

        void carer_count(int value);

        const boost::optional<boost::posix_time::ptime> &check_in() const;

        const boost::optional<boost::posix_time::ptime> &check_out() const;

        const boost::posix_time::ptime::time_duration_type &duration() const;

        const boost::optional<Location> location() const;

        void location(const rows::Location &location);

        VisitType type() const;

        void type(VisitType type);

        const boost::optional<CalendarVisit> &calendar_visit() const;

        boost::optional<CalendarVisit> &calendar_visit();

        bool operator==(const ScheduledVisit &other) const;

        bool operator!=(const ScheduledVisit &other) const;

    private:
        VisitType type_;
        boost::optional<Carer> carer_;
        boost::posix_time::ptime datetime_;
        boost::posix_time::ptime::time_duration_type duration_;
        boost::optional<boost::posix_time::ptime> check_in_;
        boost::optional<boost::posix_time::ptime> check_out_;
        boost::optional<CalendarVisit> calendar_visit_;
    };

    std::ostream &operator<<(std::ostream &out, const ScheduledVisit::VisitType &visit_type);
}

namespace rows {

    template<typename JsonType>
    ScheduledVisit ScheduledVisit::JsonLoader::Load(const JsonType &document) const {
        static const DateTime::JsonLoader datetime_loader{};
        static const CalendarVisit::JsonLoader visit_loader{};

        ScheduledVisit::VisitType visit_type{VisitType::UNKNOWN};
        const auto cancelled_it = document.find("cancelled");
        if (cancelled_it != std::end(document)) {
            if (cancelled_it.value().template get<bool>()) {
                visit_type = VisitType::CANCELLED;
            }
        }

        boost::optional<Carer> carer;
        const auto carer_it = document.find("carer");
        if (carer_it != std::end(document)) {
            const auto &carer_json = carer_it.value();
            auto sap_number_it = carer_json.find("sap_number");
            if (sap_number_it != std::end(carer_json)) {
                const auto &sap_number = sap_number_it.value().template get<std::string>();
                carer = Carer(sap_number);
            }
        }

        boost::optional<boost::posix_time::ptime> check_in;
        const auto check_in_it = document.find("check_in");
        if (check_in_it != std::end(document)) {
            const auto &check_in_json = check_in_it.value();
            if (!check_in_json.is_null()) {
                check_in = boost::date_time::parse_delimited_time<boost::posix_time::ptime>(
                        check_in_json.template get<std::string>(), 'T');
            }
        }

        boost::optional<boost::posix_time::ptime> check_out;
        const auto check_out_it = document.find("check_out");
        if (check_out_it != std::end(document)) {
            const auto &check_out_json = check_out_it.value();
            if (!check_out_json.is_null()) {
                check_out = boost::date_time::parse_delimited_time<boost::posix_time::ptime>(
                        check_out_json.template get<std::string>(), 'T');
            }
        }

        boost::posix_time::ptime datetime = datetime_loader.Load(document);

        boost::posix_time::time_duration duration;
        const auto duration_it = document.find("duration");
        if (duration_it != std::end(document)) {
            duration = boost::posix_time::seconds(std::stol(duration_it.value().template get<std::string>()));
        }

        boost::optional<CalendarVisit> calendar_visit;
        const auto visit_it = document.find("visit");
        if (visit_it != std::end(document)) {
            const auto &visit = visit_it.value();
            if (!visit.is_null()) {
                calendar_visit = visit_loader.Load(visit);
            }
        }

        return ScheduledVisit(visit_type, carer, datetime, duration, check_in, check_out, calendar_visit);
    }
}

namespace std {

    template<>
    struct hash<rows::ScheduledVisit> {
        typedef rows::ScheduledVisit argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type &object) const noexcept {
            static const std::hash<boost::posix_time::ptime> hash_date_time{};
            static const std::hash<boost::posix_time::ptime::time_duration_type> hash_duration{};
            static const std::hash<rows::Carer> hash_carer{};
            static const std::hash<rows::CalendarVisit> hash_calendar_visit{};

            std::size_t seed = 0;

            boost::hash_combine(seed, object.type());
            boost::hash_combine(seed, hash_date_time(object.datetime()));
            boost::hash_combine(seed, hash_duration(object.duration()));

            const auto &check_in = object.check_in();
            if (check_in.is_initialized()) {
                boost::hash_combine(seed, hash_date_time(check_in.get()));
            }

            const auto &check_out = object.check_out();
            if (check_out.is_initialized()) {
                boost::hash_combine(seed, hash_date_time(check_out.get()));
            }

            boost::hash_combine(seed, hash_duration(object.duration()));

            const auto &carer = object.carer();
            if (carer.is_initialized()) {
                boost::hash_combine(seed, hash_carer(carer.get()));
            }

            const auto &calendar_visit = object.calendar_visit();
            if (calendar_visit.is_initialized()) {
                boost::hash_combine(seed, hash_calendar_visit(calendar_visit.get()));
            }

            return seed;
        }
    };
}

#endif //ROWS_SCHEDULED_VISIT_H
