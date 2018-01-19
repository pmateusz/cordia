#include <sstream>
#include <unordered_map>
#include <vector>

#include <boost/format.hpp>
#include <boost/algorithm/string/join.hpp>

#include "route_validator.h"
#include "solver_wrapper.h"

namespace rows {
    std::ostream &operator<<(std::ostream &out, const rows::RouteValidator::ValidationError &error) {
        error.Print(out);
        return out;
    }

    std::ostream &operator<<(std::ostream &out, RouteValidator::ErrorCode error_code) {
        switch (error_code) {
            case RouteValidator::ErrorCode::MISSING_INFO:
                out << "MISSING_INFO";
                break;
            case RouteValidator::ErrorCode::TOO_MANY_CARERS:
                out << "TOO_MANY_CARERS";
                break;
            case RouteValidator::ErrorCode::LATE_ARRIVAL:
                out << "LATE_ARRIVAL";
                break;
            case RouteValidator::ErrorCode::BREAK_VIOLATION:
                out << "BREAK_VIOLATION";
                break;
            case RouteValidator::ErrorCode::ABSENT_CARER:
                out << "ABSENT_CARER";
                break;
            case RouteValidator::ErrorCode::UNKNOWN:
                out << "UNKNOWN";
                break;
        }
        return out;
    }

    RouteValidator::ValidationError::ValidationError(RouteValidator::ErrorCode error_code)
            : error_code_(error_code) {}

    RouteValidator::ErrorCode RouteValidator::ValidationError::error_code() const {
        return error_code_;
    }

    RouteValidator::RouteConflictError::RouteConflictError(const CalendarVisit &visit,
                                                           std::vector<rows::Route> routes)
            : ValidationError(ErrorCode::TOO_MANY_CARERS),
              visit_(visit),
              routes_(std::move(routes)) {}

    RouteValidator::ScheduledVisitError::ScheduledVisitError(ErrorCode error_code,
                                                             const ScheduledVisit &visit,
                                                             const rows::Route &route,
                                                             std::string error_message)
            : ValidationError(error_code),
              visit_(visit),
              route_(route),
              error_message_(std::move(error_message)) {}

    void RouteValidator::RouteConflictError::Print(std::ostream &out) const {
        std::vector<std::string> carers_text;
        for (const auto &route : routes_) {
            std::stringstream stream;
            stream << route.carer().sap_number();
            carers_text.emplace_back(stream.str());
        }

        out << (boost::format("RouteConflictError: visit %1% is scheduled to multiple carers: [%2%]")
                % visit_.service_user().id()
                % boost::algorithm::join(carers_text, ", ")).str();
    }

    const rows::CalendarVisit &RouteValidator::RouteConflictError::visit() const {
        return visit_;
    }

    const std::vector<rows::Route> &RouteValidator::RouteConflictError::routes() const {
        return routes_;
    }

    void RouteValidator::ScheduledVisitError::Print(std::ostream &out) const {
        out << (boost::format("ScheduledVisitError: %1%") % error_message_).str();
    }

    const rows::ScheduledVisit &RouteValidator::ScheduledVisitError::visit() const {
        return visit_;
    }

    std::vector<std::unique_ptr<rows::RouteValidator::ValidationError>>
    RouteValidator::Validate(const std::vector<rows::Route> &routes,
                             const rows::Problem &problem,
                             SolverWrapper &solver) const {
        std::vector<std::unique_ptr<rows::RouteValidator::ValidationError>> validation_errors;

        // find visits with incomplete information
        for (const auto &route: routes) {
            for (const auto &visit : route.visits()) {
                if (visit.type() == ScheduledVisit::VisitType::CANCELLED) {
                    continue;
                }

                if (!visit.calendar_visit().is_initialized()) {
                    validation_errors.emplace_back(std::make_unique<ScheduledVisitError>(
                            ErrorCode::MISSING_INFO,
                            visit,
                            route,
                            "calendar visit is missing"));
                } else if (!visit.location().is_initialized()) {
                    validation_errors.emplace_back(std::make_unique<ScheduledVisitError>(
                            ErrorCode::MISSING_INFO,
                            visit,
                            route,
                            "location is missing"));
                }
            }
        }

        // find visit assignment conflicts
        std::unordered_map<rows::CalendarVisit, std::vector<rows::Route> > visit_index;
        for (const auto &route: routes) {
            for (const auto &visit : route.visits()) {
                if (!IsAssignedAndActive(visit)) {
                    continue;
                }

                const auto &calendar_visit = visit.calendar_visit();
                const auto visit_index_it = visit_index.find(calendar_visit.get());
                if (visit_index_it != std::end(visit_index)) {
                    visit_index_it->second.push_back(route);
                } else {
                    visit_index.insert({calendar_visit.get(), std::vector<rows::Route> {route}});
                }
            }
        }

        for (const auto &visit_index_pair : visit_index) {
            if (visit_index_pair.second.size() > 1) {
                validation_errors.emplace_back(
                        std::make_unique<RouteConflictError>(visit_index_pair.first, visit_index_pair.second));
            }
        }

        // find failed arrival time
        for (const auto &route : routes) {
            const auto &visits = route.visits();

            std::vector<ScheduledVisit> visits_to_consider;
            for (const auto &visit : route.visits()) {
                if (!IsAssignedAndActive(visit)) {
                    continue;
                }
                visits_to_consider.push_back(visit);
            }

            if (visits_to_consider.size() <= 1) {
                continue;
            }

            const auto max_visit_pos = visits_to_consider.size();
            for (auto visit_pos = 1; visit_pos < max_visit_pos; ++visit_pos) {
                const auto &prev_visit = visits_to_consider[visit_pos - 1];
                const auto &visit = visits_to_consider[visit_pos];

                const auto arrival_datetime = prev_visit.datetime()
                                              + prev_visit.duration()
                                              + solver.TravelTime(prev_visit.location().get(), visit.location().get());
                if (arrival_datetime > visit.datetime()) {
                    validation_errors.emplace_back(std::make_unique<ScheduledVisitError>(
                            ErrorCode::LATE_ARRIVAL,
                            visit,
                            route,
                            (boost::format("ScheduledVisitError: carer %1% arrives with a delay of %2% to visit %3%")
                             % visit.carer().get().sap_number()
                             % (arrival_datetime - visit.datetime())
                             % visit.service_user().get().id()).str()));
                }
            }
        }

        // find visits causing time violations
        for (const auto &route : routes) {
            const auto &visits = route.visits();

            for (const auto &visit : route.visits()) {
                if (!IsAssignedAndActive(visit)) {
                    continue;
                }

                const auto &carer = visit.carer().get();
                const auto &diary = problem.diary(carer, visit.datetime().date());
                if (!diary.is_initialized()) {
                    validation_errors.emplace_back(std::make_unique<ScheduledVisitError>(
                            ErrorCode::ABSENT_CARER,
                            visit,
                            route,
                            (boost::format("ScheduledVisitError: carer %1% is absent on that day of visit %2%")
                             % carer.sap_number()
                             % visit.service_user().get().id()).str()));
                } else {
                    const auto visit_begin = visit.datetime();
                    const auto visit_end = visit.datetime() + visit.duration();

                    boost::optional<Event> working_slot;
                    for (const auto &event : diary.get().events()) {
                        if (event.end() <= visit_begin) {
                            continue;
                        }

                        if (event.begin() > visit_begin) {
                            break;
                        }

                        if (event.begin() <= visit_begin && visit_end <= event.end()) {
                            working_slot = event;
                            break;
                        }
                    }

                    if (!working_slot.is_initialized()) {
                        std::vector<Event> overlapping_slots;
                        for (const auto &event : diary.get().events()) {
                            if (event.end() <= visit_begin) {
                                continue;
                            }

                            if (event.begin() > visit_begin) {
                                break;
                            }

                            if (event.begin() <= visit_begin || visit_end <= event.end()) {
                                overlapping_slots.push_back(event);
                            }
                        }

                        if (overlapping_slots.empty()) {
                            validation_errors.emplace_back(std::make_unique<ScheduledVisitError>(
                                    ErrorCode::BREAK_VIOLATION,
                                    visit,
                                    route,
                                    (boost::format(
                                            "ScheduledVisitError: visit %1% violates contractual breaks of carer %2%.")
                                     % visit.service_user().get().id()
                                     % carer.sap_number()).str()));
                        } else {
                            std::vector<std::string> slot_texts;
                            for (const auto &event : overlapping_slots) {
                                std::stringstream message;
                                message << "[" << event.begin().time_of_day()
                                        << ", " << event.end().time_of_day()
                                        << "]";
                                slot_texts.emplace_back(message.str());
                            }

                            std::string joined_slot_text;
                            if (slot_texts.size() == 1) {
                                joined_slot_text = slot_texts[0];
                            } else {
                                joined_slot_text = boost::algorithm::join(slot_texts, ", ");
                            }

                            validation_errors.emplace_back(std::make_unique<ScheduledVisitError>(
                                    ErrorCode::BREAK_VIOLATION,
                                    visit,
                                    route,
                                    (boost::format(
                                            "ScheduledVisitError: visit %1% violates contractual breaks of carer %2%: [%3%, %4%] does not fit into %5%.")
                                     % visit.service_user().get().id()
                                     % carer.sap_number()
                                     % visit_begin.time_of_day()
                                     % visit_end.time_of_day()
                                     % joined_slot_text).str()));
                        }
                    }
                }
            }
        }

        return validation_errors;
    }

    bool rows::RouteValidator::IsAssignedAndActive(const rows::ScheduledVisit &visit) {
        return visit.calendar_visit().is_initialized()
               && visit.carer().is_initialized()
               && visit.type() != ScheduledVisit::VisitType::CANCELLED;
    }
}
