#ifndef ROWS_SOLUTION_H
#define ROWS_SOLUTION_H

#include <string>
#include <memory>
#include <vector>
#include <exception>
#include <stdexcept>

#include <libxml/xmlreader.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <glog/logging.h>

#include <boost/optional.hpp>
#include <boost/date_time.hpp>

#include <ortools/constraint_solver/routing.h>

#include "scheduled_visit.h"

namespace rows {

    class Carer;

    class Route;

    class SolverWrapper;

    class Solution {
    public:
        Solution();

        explicit Solution(std::vector<ScheduledVisit> visits);

        Solution Trim(boost::posix_time::ptime begin, boost::posix_time::ptime::time_duration_type duration) const;

        class JsonLoader : protected rows::JsonLoader {
        public:
            /*!
             * @throws std::domain_error
             */
            template<typename JsonType>
            Solution Load(const JsonType &document);
        };

        class XmlLoader {
        public:
            struct XmlDeleters {
                void operator()(xmlDocPtr doc_ptr) const {
                    xmlFreeDoc(doc_ptr);
                }

                void operator()(xmlXPathObjectPtr object_ptr) const {
                    xmlXPathFreeObject(object_ptr);
                }

                void operator()(xmlTextReaderPtr reader_ptr) const {
                    xmlFreeTextReader(reader_ptr);
                }

                void operator()(xmlXPathContextPtr context_ptr) const {
                    xmlXPathFreeContext(context_ptr);
                }

                void operator()(xmlChar *value) const {
                    xmlFree(value);
                }
            };

            XmlLoader();

            ~XmlLoader();

            Solution Load(const std::string &path);

        private:
            struct AttributeIndex {
            public:
                void Load(xmlXPathContextPtr context);

                std::string Id;
                std::string Type;
                std::string User;
                std::string SapNumber;
                std::string Longitude;
                std::string Latitude;
                std::string StartTime;
                std::string Duration;
                std::string AssignedCarer;
            };

            friend struct AttributeIndex;

            static bool NameEquals(xmlNodePtr node, const std::string &name);

            static std::string GetAttribute(xmlNodePtr node, const std::string &name);

            static std::unique_ptr<xmlXPathObject, XmlDeleters> EvalXPath(const std::string &expression,
                                                                          xmlXPathContextPtr context);

            static std::unique_ptr<xmlXPathContext, XmlDeleters> CreateXPathContext(xmlDocPtr document);
        };

        const std::vector<Carer> Carers() const;

        Route GetRoute(const Carer &carer) const;

        /**
         * Updates properties of internal visits stored by the solution object based on \p visits
         */
        void UpdateVisitProperties(const std::vector<CalendarVisit> &visits);

        const std::vector<ScheduledVisit> &visits() const;

        std::string DebugStatus(SolverWrapper &solver, const operations_research::RoutingModel &model) const;

    private:
        std::vector<ScheduledVisit> visits_;
    };
}

namespace rows {

    template<typename JsonType>
    Solution Solution::JsonLoader::Load(const JsonType &document) {
        static const ScheduledVisit::JsonLoader visit_loader{};

        auto visits_it = document.find("visits");
        if (visits_it == std::end(document)) {
            throw OnKeyNotFound("visits");
        }

        std::vector<ScheduledVisit> visits;
        for (const auto &actual_visit : visits_it.value()) {
            visits.emplace_back(visit_loader.Load(actual_visit));
        }
        return Solution(std::move(visits));
    }
}

#endif //ROWS_SOLUTION_H
