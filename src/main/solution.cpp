#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include <vector>

#include <boost/algorithm/string.hpp>
#include <boost/date_time.hpp>

#include <glog/logging.h>

#include "solution.h"
#include "route.h"
#include "solver_wrapper.h"

rows::Solution::Solution()
        : Solution(std::vector<rows::ScheduledVisit>()) {}

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

std::string Get(const std::unordered_map<std::string, std::string> &properties, const std::string &key) {
    const auto find_it = properties.find(key);
    CHECK(find_it != std::cend(properties));
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

    auto visit_sets = EvalXPath("/local:gexf/local:graph/local:nodes/*", xpath_context.get());
    if (visit_sets && !xmlXPathNodeSetIsEmpty(visit_sets->nodesetval)) {
        std::vector<rows::ScheduledVisit> visits;
        const auto node_set = visit_sets->nodesetval;
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

            std::unordered_map<std::string, std::string> properties;
            for (auto child_cursor = attvalues_collection->children; child_cursor; child_cursor = child_cursor->next) {
                if (child_cursor->type == XML_ELEMENT_NODE && NameEquals(child_cursor, "attvalue")) {
                    properties.emplace(GetAttribute(child_cursor, "for"), GetAttribute(child_cursor, "value"));
                }
            }

            const auto type_property_it = properties.find(attributes.Type);
            if (type_property_it == std::cend(properties) || type_property_it->second != "visit") {
                continue;
            }

            boost::optional<rows::Carer> carer = boost::none;
            const auto carer_find_it = properties.find(attributes.AssignedCarer);
            if (carer_find_it != std::cend(properties)) {
                CHECK(!carer_find_it->second.empty());
                carer = rows::Carer(carer_find_it->second);
            }

            const auto duration = GetCheckNotEmpty(properties, attributes.Duration);
            const auto start_time = GetCheckNotEmpty(properties, attributes.StartTime);
            rows::ScheduledVisit visit(rows::ScheduledVisit::VisitType::OK,
                                       std::move(carer),
                                       boost::posix_time::not_a_date_time, // TODO: visits must happen on a specific day
                                       boost::posix_time::duration_from_string(duration),
                                       boost::none,
                                       boost::none,
                                       boost::none); // TODO: calendar visit must be present
            visits.push_back(visit);
        }

        return rows::Solution(visits);
    }

    return {};
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
    DCHECK_EQ(xmlXPathRegisterNs(xpath_context.get(),
                                 reinterpret_cast<xmlChar const *> ("local"),
                                 reinterpret_cast<xmlChar const *> ("http://www.gexf.net/1.1draft")), 0);
    DCHECK_EQ(xmlXPathRegisterNs(xpath_context.get(),
                                 reinterpret_cast<xmlChar const *> ("xsi"),
                                 reinterpret_cast<xmlChar const *> ("http://www.w3.org/2001/XMLSchema-instance")), 0);
    return xpath_context;
}

void rows::Solution::XmlLoader::AttributeIndex::Load(xmlXPathContextPtr context) {
    std::unordered_map<std::string, std::string> node_property_index;
    auto attribute_set = EvalXPath("/local:gexf/local:graph/local:attributes[@class='node']/*", context);
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

    Type = GetCheckNotEmpty(node_property_index, "type");
    AssignedCarer = GetCheckNotEmpty(node_property_index, "assigned_carer");
    User = GetCheckNotEmpty(node_property_index, "user");
    Longitude = GetCheckNotEmpty(node_property_index, "longitude");
    Latitude = GetCheckNotEmpty(node_property_index, "latitude");
    StartTime = GetCheckNotEmpty(node_property_index, "start_time");
    Duration = GetCheckNotEmpty(node_property_index, "duration");
}
