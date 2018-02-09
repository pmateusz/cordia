#include <glog/logging.h>

#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <sstream>

#include <boost/algorithm/string.hpp>

#include "solution.h"
#include "route.h"
#include "solver_wrapper.h"

rows::Solution::Solution(std::vector<rows::ScheduledVisit> visits)
        : visits_(std::move(visits)) {}

const std::vector<rows::ScheduledVisit> &rows::Solution::visits() const {
    return visits_;
}

rows::Route rows::Solution::GetRoute(const rows::Carer &carer) const {
    std::vector<rows::ScheduledVisit> carer_visits;

    for (const auto &visit : visits_) {
        if (visit.calendar_visit()
            && carer == visit.carer()
            && (visit.type() != ScheduledVisit::VisitType::CANCELLED
                && visit.type() != ScheduledVisit::VisitType::INVALID
                && visit.type() != ScheduledVisit::VisitType::MOVED)) {
            carer_visits.push_back(visit);
        }
    }

    std::sort(std::begin(carer_visits),
              std::end(carer_visits),
              [](const rows::ScheduledVisit &left, const rows::ScheduledVisit &right) -> bool {
                  return left.datetime() <= right.datetime();
              });

    return {carer, std::move(carer_visits)};
}

rows::Solution rows::Solution::Trim(boost::posix_time::ptime begin,
                                    boost::posix_time::ptime::time_duration_type duration) const {
    std::vector<rows::ScheduledVisit> visits_to_use;

    const auto end = begin + duration;
    for (const auto &visit : visits_) {
        if (begin <= visit.datetime() && visit.datetime() < end) {
            visits_to_use.push_back(visit);
        }
    }

    return Solution(visits_to_use);
}

const std::vector<rows::Carer> rows::Solution::Carers() const {
    std::unordered_set<rows::Carer> carers;

    for (const auto &visit: visits_) {
        const auto &carer = visit.carer();
        if (carer) {
            carers.insert(carer.get());
        }
    }

    return {std::cbegin(carers), std::cend(carers)};
}

struct PartialVisitContainerContract {

    std::size_t operator()(const rows::CalendarVisit &object) const noexcept {
        static const std::hash<rows::Address> hash_address{};
        static const std::hash<rows::ServiceUser> hash_service_user{};
        static const std::hash<boost::posix_time::ptime> hash_date_time{};
        static const std::hash<boost::posix_time::ptime::time_duration_type> hash_duration{};

        std::size_t seed = 0;
        boost::hash_combine(seed, hash_address(object.address()));
        boost::hash_combine(seed, hash_service_user(object.service_user()));
        boost::hash_combine(seed, hash_date_time(object.datetime()));
        boost::hash_combine(seed, hash_duration(object.duration()));

        return seed;
    }

    bool operator()(const rows::CalendarVisit &left, const rows::CalendarVisit &right) const noexcept {
        return left.address() == right.address()
               && left.service_user() == right.service_user()
               && left.datetime() == right.datetime()
               && left.duration() == right.duration();
    }
};

void rows::Solution::UpdateVisitLocations(const std::vector<rows::CalendarVisit> &visits) {
    std::unordered_map<rows::ServiceUser, rows::Location> location_index;
    for (const auto &visit : visits) {
        const auto &location = visit.location();
        if (!location.is_initialized()) {
            continue;
        }

        const auto find_it = location_index.find(visit.service_user());
        if (find_it == std::end(location_index)) {
            location_index.emplace(visit.service_user(), location.get());
        }
    }

    std::unordered_set<rows::CalendarVisit, PartialVisitContainerContract, PartialVisitContainerContract> visit_index;
    for (const auto &visit : visits) {
        visit_index.emplace(visit);
    }

    for (auto &visit : visits_) {
        const auto &calendar_visit = visit.calendar_visit();
        if (!calendar_visit.is_initialized()) {
            continue;
        }

        const auto location_index_it = location_index.find(calendar_visit.get().service_user());
        if (location_index_it != std::end(location_index)) {
            if (visit.location().is_initialized()) {
                DCHECK_EQ(visit.location().get(), location_index_it->second);
            } else {
                visit.location(location_index_it->second);
            }
        }

        const auto carer_count_it = visit_index.find(calendar_visit.get());
        if (carer_count_it != std::cend(visit_index)) {
            visit.carer_count(carer_count_it->carer_count());
        }
    }
}

std::string rows::Solution::DebugStatus(rows::SolverWrapper &solver,
                                        const operations_research::RoutingModel &model) const {
    std::stringstream status_stream;

    const auto visits_with_no_calendar = std::count_if(std::cbegin(visits_),
                                                       std::cend(visits_),
                                                       [](const rows::ScheduledVisit &visit) -> bool {
                                                           return visit.calendar_visit().is_initialized();
                                                       });

    status_stream << "Visits with no calendar event: " << visits_with_no_calendar
                  << " of " << visits_.size()
                  << " total, ratio: " << static_cast<double>(visits_with_no_calendar) / visits_.size()
                  << std::endl;

    const auto routes = solver.GetRoutes(*this, model);
    DCHECK_EQ(routes.size(), model.vehicles());

    for (int vehicle = 0; vehicle < model.vehicles(); ++vehicle) {
        const auto &node_route = routes[vehicle];
        const auto carer = solver.Carer(vehicle);

        status_stream << "Route " << vehicle << " " << carer << ":" << std::endl;
        if (node_route.empty()) {
            continue;
        }

        for (const auto &node : node_route) {
            status_stream << node << std::endl;
        }
    }

    return status_stream.str();
}
