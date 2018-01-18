#include "solution.h"

#include <glog/logging.h>

#include <algorithm>
#include <unordered_set>
#include <unordered_map>

rows::Solution::Solution(std::vector<rows::ScheduledVisit> visits)
        : visits_(std::move(visits)) {}

const std::vector<rows::ScheduledVisit> &rows::Solution::visits() const {
    return visits_;
}

rows::Route rows::Solution::GetRoute(const rows::Carer &carer) const {
    std::vector<rows::ScheduledVisit> carer_visits;

    for (const auto &visit : visits_) {
        if (visit.carer() && carer == visit.carer() && visit.type() != ScheduledVisit::VisitType::CANCELLED) {
            carer_visits.push_back(visit);
        }
    }

    std::sort(std::begin(carer_visits),
              std::end(carer_visits),
              [](const rows::ScheduledVisit &left, const rows::ScheduledVisit &right) -> bool {
                  return left.datetime() <= right.datetime();
              });
    return Route{carer, carer_visits};
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

void rows::Solution::UpdateVisitLocations(const std::vector<rows::CalendarVisit> &visits) {
    std::unordered_map<rows::ServiceUser, rows::Location> location_index;
    for (const auto &visit : visits) {
        const auto &location = visit.location();
        if (location.is_initialized()) {
            location_index.insert(std::make_pair(visit.service_user(), location.get()));
        }
    }

    for (auto &visit : visits_) {
        const auto &calendar_visit = visit.calendar_visit();
        if (!calendar_visit.is_initialized()) {
            continue;
        }

        if (visit.location().is_initialized()) {
            continue;
        }

        const auto location_index_it = location_index.find(calendar_visit.get().service_user());
        if (location_index_it != std::end(location_index)) {
            visit.location(location_index_it->second);
        }
    }
}

rows::Solution rows::Solution::Resolve(
        const std::vector<std::unique_ptr<rows::RouteValidator::ValidationError> > &validation_errors) const {
    auto visits_to_use = visits_;

    // TODO: implement this code differently
    for (const auto &error : validation_errors) {
        switch (error->error_code()) {
            case RouteValidator::ErrorCode::ABSENT_CARER:
            case RouteValidator::ErrorCode::BREAK_VIOLATION:
            case RouteValidator::ErrorCode::MISSING_INFO:
            case RouteValidator::ErrorCode::LATE_ARRIVAL: {
                const rows::RouteValidator::ScheduledVisitError &error_to_use
                        = dynamic_cast<const rows::RouteValidator::ScheduledVisitError &>(*error);
                auto reset = false;
                for (auto &visit : visits_to_use) {
                    if (visit == error_to_use.visit()) {
                        LOG(INFO) << "reset " << visit;
                        visit.carer().reset();
                        reset = true;
                    }
                }
                if (!reset) {
                    LOG(ERROR) << error_to_use << " " << error_to_use.error_code() << " " << error_to_use.visit();
                }
                break;
            }
            case RouteValidator::ErrorCode::TOO_MANY_CARERS: {
                const rows::RouteValidator::RouteConflictError &error_to_use
                        = dynamic_cast<const rows::RouteValidator::RouteConflictError &>(*error);
                const auto &calendar_visit = error_to_use.visit();
                const auto route_end_it = std::end(error_to_use.routes());
                auto route_it = std::begin(error_to_use.routes());

                const auto &predicate = [&calendar_visit](const rows::ScheduledVisit &visit) -> bool {
                    const auto &local_calendar_visit = visit.calendar_visit();
                    return local_calendar_visit.is_initialized() && local_calendar_visit.get() == calendar_visit;
                };

                for (; route_it != route_end_it; ++route_it) {
                    const auto visit_route_it = std::find_if(std::begin(route_it->visits()),
                                                             std::end(route_it->visits()),
                                                             predicate);
                    if (visit_route_it != std::end(route_it->visits())) {
                        if (visit_route_it->carer().is_initialized()) {
                            ++route_it;
                            break;
                        }
                    }
                }

                for (; route_it != route_end_it; ++route_it) {
                    const auto visit_route_it = std::find_if(std::begin(route_it->visits()),
                                                             std::end(route_it->visits()),
                                                             predicate);
                    for (auto &visit : visits_to_use) {
                        if (visit == *visit_route_it) {
                            LOG(INFO) << "reset " << visit;
                            visit.carer().reset();
                        }
                    }
                }
                break;
            }
            default:
                VLOG(1) << "Error code:" << error->error_code() << " ignored";
                continue;
        }
    }

    return rows::Solution(std::move(visits_to_use));
}
