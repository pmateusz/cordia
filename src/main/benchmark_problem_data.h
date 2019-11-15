#ifndef ROWS_BENCHMARK_PROBLEM_DATA_H
#define ROWS_BENCHMARK_PROBLEM_DATA_H

#include <unordered_map>
#include <ortools/constraint_solver/routing_index_manager.h>

#include "problem_data.h"
#include "calendar_visit.h"
#include "problem.h"

namespace rows {

    class BenchmarkProblemData : public ProblemData {
    public:
        BenchmarkProblemData(Problem problem,
                             boost::posix_time::time_period time_horizon,
                             int64 carer_used_penalty,
                             std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, CalendarVisit> node_index,
                             std::unordered_map<CalendarVisit,
                                     std::vector<operations_research::RoutingIndexManager::NodeIndex>,
                                     Problem::PartialVisitOperations,
                                     Problem::PartialVisitOperations> visit_index,
                             std::vector<std::vector<int>> distance_matrix);

        int vehicles() const;

        int nodes() const;

        boost::posix_time::time_duration VisitStart(operations_research::RoutingNodeIndex node) const;

        boost::posix_time::time_duration TotalWorkingHours(int vehicle, boost::gregorian::date date) const;

        int64 Distance(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const;

        int64 ServiceTime(operations_research::RoutingNodeIndex node) const;

        int64 ServicePlusTravelTime(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const;

        const std::vector<operations_research::RoutingNodeIndex> &GetNodes(const rows::CalendarVisit &visit) const;

        const std::vector<operations_research::RoutingNodeIndex> &GetNodes(operations_research::RoutingNodeIndex node) const;

        const rows::CalendarVisit &NodeToVisit(const operations_research::RoutingNodeIndex &node) const;

        boost::posix_time::ptime StartHorizon() const override;

        boost::posix_time::ptime EndHorizon() const override;

        bool Contains(const CalendarVisit &visit) const override;

        const Problem &problem() const override;

        int64 GetDroppedVisitPenalty() const override;

    private:
        Problem problem_;

        boost::posix_time::time_period time_horizon_;

        int64 carer_used_penalty_;

        std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, CalendarVisit> node_index_;
        std::unordered_map<CalendarVisit,
                std::vector<operations_research::RoutingIndexManager::NodeIndex>,
                Problem::PartialVisitOperations,
                Problem::PartialVisitOperations> visit_index_;
        std::vector<std::vector<int>> distance_matrix_;
    };

    class BenchmarkProblemDataFactory : public ProblemDataFactory {
    public:
        static BenchmarkProblemDataFactory Load(const std::string &file_path);

        std::shared_ptr<ProblemData> operator()() const;

        std::shared_ptr<ProblemData> operator()(Problem problem) const override;

    private:
        BenchmarkProblemDataFactory(std::vector<rows::ExtendedServiceUser> users,
                                    std::vector<rows::CalendarVisit> calendar_visits,
                                    std::vector<std::pair<rows::Carer, std::vector<rows::Diary>>> carers,
                                    boost::posix_time::time_period time_horizon,
                                    int64 carer_used_penalty,
                                    std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, rows::CalendarVisit> node_index,
                                    std::unordered_map<rows::CalendarVisit,
                                            std::vector<operations_research::RoutingIndexManager::NodeIndex>,
                                            rows::Problem::PartialVisitOperations,
                                            rows::Problem::PartialVisitOperations> visit_index,
                                    std::vector<std::vector<int>> distance_matrix);

        std::vector<rows::ExtendedServiceUser> users_;
        std::vector<rows::CalendarVisit> calendar_visits_;
        std::vector<std::pair<rows::Carer, std::vector<rows::Diary>>> carers_;
        boost::posix_time::time_period time_horizon_;
        int64 carer_used_penalty_;
        std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, rows::CalendarVisit> node_index_;
        std::unordered_map<rows::CalendarVisit,
                std::vector<operations_research::RoutingIndexManager::NodeIndex>,
                rows::Problem::PartialVisitOperations,
                rows::Problem::PartialVisitOperations> visit_index_;
        std::vector<std::vector<int>> distance_matrix_;
    };
}


#endif //ROWS_BENCHMARK_PROBLEM_DATA_H
