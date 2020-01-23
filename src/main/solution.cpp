#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/date_time.hpp>

#include <glog/logging.h>
#include <util/aplication_error.h>

#include "break.h"
#include "route.h"
#include "scheduled_visit.h"
#include "solution.h"
#include "solver_wrapper.h"

rows::Solution::Solution()
        : Solution(std::vector<ScheduledVisit>(), std::vector<Break>()) {}

rows::Solution::Solution(std::vector<ScheduledVisit> visits, std::vector<Break> breaks)
        : visits_(std::move(visits)),
          breaks_(std::move(breaks)) {}

const std::vector<rows::ScheduledVisit> &rows::Solution::visits() const {
    return visits_;
}

const std::vector<rows::Break> &rows::Solution::breaks() const {
    return breaks_;
}

rows::Route rows::Solution::GetRoute(const rows::Carer &carer) const {
    std::vector<rows::ScheduledVisit> carer_visits;

    for (const auto &visit : visits_) {
        if (visit.calendar_visit()
            && carer.sap_number() == visit.carer()->sap_number()
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

rows::Solution rows::Solution::Trim(boost::posix_time::ptime begin, boost::posix_time::ptime end) const {
    std::vector<ScheduledVisit> visits_to_use;
    for (const auto &visit : visits_) {
        if (begin <= visit.datetime() && visit.datetime() <= end) {
            visits_to_use.push_back(visit);
        }
    }

    std::vector<Break> breaks_to_use;
    for (const auto &break_element : breaks_) {
        if (begin <= break_element.datetime() && break_element.datetime() < end) {
            breaks_to_use.push_back(break_element);
        }
    }
    return Solution(std::move(visits_to_use), std::move(breaks_to_use));
}

const std::vector<rows::Carer> rows::Solution::Carers() const {
    std::unordered_set<rows::Carer> carers;

    for (const auto &visit: visits_) {
        const auto &carer = visit.carer();
        if (carer) {
            carers.insert(carer.get());
        }
    }

    std::vector<rows::Carer> sorted_carers{std::begin(carers), std::end(carers)};
    std::sort(std::begin(sorted_carers), std::end(sorted_carers),
              [](const rows::Carer &left, const rows::Carer &right) -> bool {
                  return std::stoll(left.sap_number()) <= std::stoll(right.sap_number());
              });
    return sorted_carers;
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

void rows::Solution::UpdateVisitProperties(const std::vector<rows::CalendarVisit> &visits) {
    std::unordered_map<rows::ServiceUser, rows::Location> location_index;
    std::unordered_map<rows::ServiceUser, rows::Address> address_index;
    std::unordered_map<rows::ServiceUser, std::vector<rows::CalendarVisit> > visit_index;
    for (const auto &visit : visits) {
        const auto &location = visit.location();
        if (location.is_initialized()) {
            const auto find_it = location_index.find(visit.service_user());
            if (find_it == std::end(location_index)) {
                location_index.emplace(visit.service_user(), location.get());
            }
        }

        if (visit.address() != Address::DEFAULT) {
            const auto find_it = address_index.find(visit.service_user());
            if (find_it == std::end(address_index)) {
                address_index.emplace(visit.service_user(), visit.address());
            }
        }

        auto visit_it = visit_index.find(visit.service_user());
        if (visit_it == std::end(visit_index)) {
            auto insert_pair = visit_index.emplace(visit.service_user(), std::vector<rows::CalendarVisit>());
            CHECK(insert_pair.second);
            visit_it = insert_pair.first;
        }

        visit_it->second.push_back(visit);
    }

    for (auto &visit : visits_) {
        auto &calendar_visit = visit.calendar_visit();
        if (!calendar_visit.is_initialized()) {
            continue;
        }

        const auto location_index_it = location_index.find(calendar_visit->service_user());
        if (location_index_it != std::end(location_index)) {
            if (visit.location().is_initialized()) {
                DCHECK_EQ(visit.location().get(), location_index_it->second);
            } else {
                visit.location(location_index_it->second);
            }
        }

        const auto address_index_it = address_index.find(calendar_visit->service_user());
        if (address_index_it != std::end(address_index)) {
            if (visit.address().is_initialized()) {
                DCHECK_EQ(visit.address().get(), address_index_it->second);
            } else {
                visit.address(address_index_it->second);
            }
        }

        const auto start_time_distance = [&visit](const rows::CalendarVisit &left,
                                                  const rows::CalendarVisit &right) -> bool {
            auto left_duration = (visit.datetime() - left.datetime());
            if (left_duration.is_negative()) {
                left_duration = left_duration.invert_sign();
            }

            auto right_duration = (visit.datetime() - right.datetime());
            if (right_duration.is_negative()) {
                right_duration = right_duration.invert_sign();
            }

            return left_duration.total_seconds() <= right_duration.total_seconds();
        };

        // find min date in visit index
        const auto calendar_visits_pair_it = visit_index.find(visit.service_user().get());
        if (calendar_visits_pair_it == std::end(visit_index)) {
            // solution has a user who is not present in the problem
            // this situation may happen when a warm start solution does not match original problem
            continue;
        }

        const auto closest_visit_it = std::min_element(std::begin(calendar_visits_pair_it->second),
                                                       std::end(calendar_visits_pair_it->second),
                                                       start_time_distance);
        CHECK(closest_visit_it != std::end(calendar_visits_pair_it->second));
        calendar_visit->datetime(closest_visit_it->datetime());
        calendar_visit->duration(closest_visit_it->duration());
    }
}

std::string rows::Solution::DebugStatus(rows::SolverWrapper &solver, const operations_research::RoutingModel &model) const {
    std::stringstream status_stream;

    const auto visits_with_calendar = std::count_if(std::begin(visits_),
                                                    std::end(visits_),
                                                    [](const rows::ScheduledVisit &visit) -> bool {
                                                        return visit.calendar_visit().is_initialized();
                                                    });

    status_stream << "Visits with calendar event: " << visits_with_calendar
                  << " of " << visits_.size()
                  << " total, ratio: " << static_cast<double>(visits_with_calendar) / visits_.size()
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

std::string Get(const std::unordered_map<std::string, std::string> &properties, const std::string &key) {
    const auto find_it = properties.find(key);
    CHECK(find_it != std::end(properties));
    return find_it->second;
}

std::string GetCheckNotEmpty(const std::unordered_map<std::string, std::string> &properties, const std::string &key) {
    const auto value = Get(properties, key);
    CHECK(!value.empty());
    return value;
}

rows::Solution::XmlLoader::XmlLoader() {
    LIBXML_TEST_VERSION

    xmlInitParser();
}

rows::Solution::XmlLoader::~XmlLoader() {
    xmlCleanupParser();
}

rows::Solution rows::Solution::XmlLoader::Load(const std::string &path) {
#if !defined(LIBXML_XPATH_ENABLED) || !defined(LIBXML_SAX1_ENABLED)
#error LIBXML library version referenced by this project does not support XPath
#endif

    std::unique_ptr<xmlDoc, XmlDeleters> document{xmlParseFile(path.c_str())};
    auto xpath_context = CreateXPathContext(document.get());

    AttributeIndex attributes;
    attributes.Load(xpath_context.get());

    std::unordered_map<std::string, rows::Carer> carers;
    std::unordered_map<std::string, rows::ScheduledVisit> visits;
    std::unordered_map<std::string, rows::ServiceUser> users;
    std::unordered_map<std::string, rows::Break> breaks;

    auto nodes_set = EvalXPath("/ns:gexf/ns:graph/ns:nodes/*", xpath_context.get());
    if (nodes_set && !xmlXPathNodeSetIsEmpty(nodes_set->nodesetval)) {
        const auto node_set = nodes_set->nodesetval;
        for (auto item = 0; item < node_set->nodeNr; ++item) {
            const auto &node = node_set->nodeTab[item];
            if (node->type != XML_ELEMENT_NODE || !NameEquals(node, "node")) {
                continue;
            }

            xmlNodePtr attvalues_collection = nullptr;
            for (auto child_cursor = node->children; child_cursor; child_cursor = child_cursor->next) {
                if (child_cursor->type == XML_ELEMENT_NODE && NameEquals(child_cursor, "attvalues")) {
                    attvalues_collection = child_cursor;
                    break;
                }
            }

            if (!attvalues_collection || GetAttribute(node, "label") == "depot") {
                continue;
            }

            const auto node_id = GetAttribute(node, "id");
            CHECK(!node_id.empty());

            std::unordered_map<std::string, std::string> properties;
            for (auto child_cursor = attvalues_collection->children; child_cursor; child_cursor = child_cursor->next) {
                if (child_cursor->type == XML_ELEMENT_NODE && NameEquals(child_cursor, "attvalue")) {
                    properties.emplace(GetAttribute(child_cursor, "for"), GetAttribute(child_cursor, "value"));
                }
            }

            const auto type_property_it = properties.find(attributes.Type);
            if (type_property_it == std::end(properties)) {
                continue;
            }

            if (type_property_it->second == "visit") {
                boost::optional<rows::Carer> carer = boost::none;
                const auto carer_find_it = properties.find(attributes.AssignedCarer);
                if (carer_find_it != std::end(properties)) {
                    CHECK(!carer_find_it->second.empty());
                    carer = rows::Carer(carer_find_it->second);
                }

                const auto id = std::stoul(GetCheckNotEmpty(properties, attributes.Id));
                const auto duration = boost::posix_time::duration_from_string(
                        GetCheckNotEmpty(properties, attributes.Duration));
                const auto start_time = boost::posix_time::time_from_string(
                        GetCheckNotEmpty(properties, attributes.StartTime));
                visits.emplace(node_id, rows::ScheduledVisit{rows::ScheduledVisit::VisitType::OK,
                                                             std::move(carer),
                                                             start_time,
                                                             duration,
                                                             boost::none,
                                                             boost::none,
                                                             rows::CalendarVisit{
                                                                     id,
                                                                     rows::ServiceUser::DEFAULT,
                                                                     rows::Address::DEFAULT,
                                                                     rows::Location(
                                                                             GetCheckNotEmpty(properties,
                                                                                              attributes.Latitude),
                                                                             GetCheckNotEmpty(properties,
                                                                                              attributes.Longitude)
                                                                     ),
                                                                     start_time,
                                                                     duration,
                                                                     0,
                                                                     std::vector<int>{}}});
            } else if (type_property_it->second == "break") {
                const auto carer_find_it = properties.find(attributes.AssignedCarer);
                CHECK(carer_find_it != std::end(properties));
                CHECK(!carer_find_it->second.empty());
                rows::Carer carer = rows::Carer(carer_find_it->second);

                const auto start_time = boost::posix_time::time_from_string(
                        GetCheckNotEmpty(properties, attributes.StartTime));

                const auto duration = boost::posix_time::duration_from_string(
                        GetCheckNotEmpty(properties, attributes.Duration));

                breaks.emplace(node_id, Break(std::move(carer), start_time, duration));
            } else if (type_property_it->second == "user") {
                users.emplace(node_id, rows::ServiceUser{std::stol(GetCheckNotEmpty(properties, attributes.Id))});
            } else if (type_property_it->second == "carer") {
                carers.emplace(node_id, rows::Carer{GetCheckNotEmpty(properties, attributes.SapNumber)});
            } else {
                throw util::ApplicationError((boost::format("Unknown node type: %1%") % type_property_it->second).str(),
                                             util::ErrorCode::ERROR);
            }
        }
    }

    auto edges_set = EvalXPath("/ns:gexf/ns:graph/ns:edges/*", xpath_context.get());
    if (edges_set && !xmlXPathNodeSetIsEmpty(edges_set->nodesetval)) {
        const auto edge_set = edges_set->nodesetval;
        for (auto item = 0; item < edge_set->nodeNr; ++item) {
            const auto &edge = edge_set->nodeTab[item];
            if (edge->type != XML_ELEMENT_NODE || !NameEquals(edge, "edge")) {
                continue;
            }

            const auto source = GetAttribute(edge, "source");
            const auto target = GetAttribute(edge, "target");
            const auto visit_it = visits.find(target);
            if (visit_it == std::end(visits)) {
                continue;
            }

            const auto carer_it = carers.find(source);
            if (carer_it != std::end(carers)) {
                visit_it->second.carer() = carer_it->second;
                visit_it->second.calendar_visit()->carer_count(1);
            } else {
                const auto user_it = users.find(source);
                if (user_it != std::end(users)) {
                    visit_it->second.calendar_visit()->service_user() = user_it->second;
                }
            }
        }
    }

    std::vector<rows::ScheduledVisit> assigned_visits;
    for (const auto &visit_pair : visits) {
        const auto &visit = visit_pair.second;

        if (!visit.carer().is_initialized()) {
            continue;
        }

        if (visit.type() != ScheduledVisit::VisitType::OK || !visit.service_user().is_initialized()) {
            LOG(WARNING) << boost::format("Visit {0} is not fully initialized") % visit;
            continue;
        }

        assigned_visits.push_back(visit);
    }

    std::vector<rows::Break> assigned_breaks;
    for (const auto &break_item : breaks) {
        assigned_breaks.push_back(break_item.second);
    }

    return rows::Solution(std::move(assigned_visits), std::move(assigned_breaks));
}

std::unique_ptr<xmlXPathObject, rows::Solution::XmlLoader::XmlDeleters>
rows::Solution::XmlLoader::EvalXPath(const std::string &expression, xmlXPathContextPtr context) {
    return std::unique_ptr<xmlXPathObject, rows::Solution::XmlLoader::XmlDeleters>{
            xmlXPathEvalExpression(reinterpret_cast<xmlChar const *> (expression.c_str()), context)};
}

std::string rows::Solution::XmlLoader::GetAttribute(xmlNodePtr node, const std::string &name) {
    std::unique_ptr<xmlChar, XmlDeleters> value{
            xmlGetProp(node, reinterpret_cast<xmlChar const *>(name.c_str()))};

    if (value) {
        return {reinterpret_cast<char const *>(value.get())};
    }
    return {};
}

bool rows::Solution::XmlLoader::NameEquals(xmlNodePtr node, const std::string &name) {
    return xmlStrEqual(node->name, reinterpret_cast<xmlChar const *>(name.c_str())) != 0;
}

std::unique_ptr<xmlXPathContext, rows::Solution::XmlLoader::XmlDeleters>
rows::Solution::XmlLoader::CreateXPathContext(xmlDocPtr document) {
    std::unique_ptr<xmlXPathContext, XmlDeleters> xpath_context{xmlXPathNewContext(document)};
    CHECK_EQ(xmlXPathRegisterNs(xpath_context.get(),
                                reinterpret_cast<xmlChar const *> ("ns"),
                                reinterpret_cast<xmlChar const *> ("http://www.gexf.net/1.1draft")), 0)
        << "Failed to register the namespace http://www.gexf.net/1.1draft";
    return xpath_context;
}

void rows::Solution::XmlLoader::AttributeIndex::Load(xmlXPathContextPtr context) {
    std::unordered_map<std::string, std::string> node_property_index;
    auto attribute_set = EvalXPath("/ns:gexf/ns:graph/ns:attributes[@class='node']/*", context);
    if (attribute_set && !xmlXPathNodeSetIsEmpty(attribute_set->nodesetval)) {
        const auto node_set = attribute_set->nodesetval;
        for (auto item = 0; item < node_set->nodeNr; ++item) {
            const auto id_value = GetAttribute(node_set->nodeTab[item], "id");
            const auto title_value = GetAttribute(node_set->nodeTab[item], "title");

            CHECK(!id_value.empty());
            CHECK(!title_value.empty());
            node_property_index.emplace(title_value, id_value);
        }
    }

    Id = GetCheckNotEmpty(node_property_index, "id");
    Type = GetCheckNotEmpty(node_property_index, "type");
    User = GetCheckNotEmpty(node_property_index, "user");
    SapNumber = GetCheckNotEmpty(node_property_index, "sap_number");
    Longitude = GetCheckNotEmpty(node_property_index, "longitude");
    Latitude = GetCheckNotEmpty(node_property_index, "latitude");
    StartTime = GetCheckNotEmpty(node_property_index, "start_time");
    Duration = GetCheckNotEmpty(node_property_index, "duration");
    AssignedCarer = GetCheckNotEmpty(node_property_index, "assigned_carer");
}
