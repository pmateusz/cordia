#include "benchmark_problem_data.h"

#include <ampl/ampl.h>

#include <utility>


static std::vector<int> MemberRangeToVector(const ampl::Set::MemberRange &member_range) {
    std::vector<int> values;
    for (const auto &value : member_range) {
        values.push_back(value[0].dbl());
    }
    return values;
}

static std::vector<int> RowToVector(const ampl::DataFrame::Row &row) {
    std::vector<int> values;
    for (decltype(row.size()) index = 0; index < row.size(); ++index) {
        values.emplace_back(row[index].dbl());
    }
    return values;
}

static std::unordered_map<int, int> DataFrameToMap(const ampl::DataFrame &data_frame, std::size_t index_column, std::size_t series_column) {
    std::unordered_map<int, int> bundle;
    for (const auto &row: data_frame) {
        bundle.emplace(row[index_column].dbl(), row[series_column].dbl());
    }
    return bundle;
}

rows::BenchmarkProblemData::BenchmarkProblemData(rows::Problem problem,
                                                 boost::posix_time::time_period time_horizon,
                                                 int64 carer_used_penalty,
                                                 std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, CalendarVisit> node_index,
                                                 std::unordered_map<CalendarVisit, std::vector<operations_research::RoutingIndexManager::NodeIndex>, Problem::PartialVisitOperations, Problem::PartialVisitOperations> visit_index,
                                                 std::vector<std::vector<int>> distance_matrix)
        : problem_{std::move(problem)},
          carer_used_penalty_{carer_used_penalty},
          time_horizon_{time_horizon},
          node_index_{std::move(node_index)},
          visit_index_{std::move(visit_index)},
          distance_matrix_{std::move(distance_matrix)} {}

int rows::BenchmarkProblemData::vehicles() const {
    return static_cast<int>(problem_.carers().size());
}

int rows::BenchmarkProblemData::nodes() const {
    return static_cast<int>(node_index_.size() + 1); // depot not included
}

boost::posix_time::time_duration rows::BenchmarkProblemData::VisitStart(operations_research::RoutingNodeIndex node) const {
    return NodeToVisit(node).datetime() - time_horizon_.begin();
}

boost::posix_time::time_duration rows::BenchmarkProblemData::TotalWorkingHours(int vehicle, boost::gregorian::date date) const {
    const auto &carer = problem_.carers().at(vehicle).first;
    const auto &diary_opt = problem_.diary(carer, date);

    if (diary_opt.is_initialized()) {
        const auto &diary = diary_opt.get();
        return diary.duration();
    }

    return boost::posix_time::seconds(0);
}

int64 rows::BenchmarkProblemData::Distance(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const {
    operations_research::RoutingNodeIndex from_node = from;
    if (from_node != DEPOT) {
        from_node = NodeToVisit(from).id();
    }

    operations_research::RoutingNodeIndex to_node = to;
    if (to_node != DEPOT) {
        to_node = NodeToVisit(to).id();
    }

    return distance_matrix_.at(from_node.value()).at(to_node.value());
}

int64 rows::BenchmarkProblemData::ServiceTime(operations_research::RoutingNodeIndex node) const {
    if (node == DEPOT) {
        return 0;
    }

    const auto visit = NodeToVisit(node);
    return visit.duration().total_seconds();
}

int64 rows::BenchmarkProblemData::ServicePlusTravelTime(operations_research::RoutingNodeIndex from, operations_research::RoutingNodeIndex to) const {
    if (from == DEPOT) {
        return 0;
    }

    const auto service_time = ServiceTime(from);
    const auto travel_time = Distance(from, to);
    return service_time + travel_time;
}

const std::vector<operations_research::RoutingNodeIndex> &rows::BenchmarkProblemData::GetNodes(const rows::CalendarVisit &visit) const {
    const auto find_it = visit_index_.find(visit);
    CHECK(find_it != std::end(visit_index_));
    CHECK(!find_it->second.empty());
    return find_it->second;
}

const std::vector<operations_research::RoutingNodeIndex> &rows::BenchmarkProblemData::GetNodes(operations_research::RoutingNodeIndex node) const {
    return GetNodes(NodeToVisit(node));
}

const rows::CalendarVisit &rows::BenchmarkProblemData::NodeToVisit(const operations_research::RoutingNodeIndex &node) const {
    DCHECK_NE(DEPOT, node);

    return node_index_.at(node);
}

boost::posix_time::ptime rows::BenchmarkProblemData::StartHorizon() const {
    return time_horizon_.begin();
}

boost::posix_time::ptime rows::BenchmarkProblemData::EndHorizon() const {
    return time_horizon_.end();
}

bool rows::BenchmarkProblemData::Contains(const rows::CalendarVisit &visit) const {
    return visit_index_.find(visit) != std::end(visit_index_);
}

const rows::Problem &rows::BenchmarkProblemData::problem() const {
    return problem_;
}

int64 rows::BenchmarkProblemData::GetDroppedVisitPenalty() const {
    return 2 * carer_used_penalty_;
}

std::shared_ptr<rows::BenchmarkProblemData> rows::BenchmarkProblemDataFactory::operator()() const {
    return std::make_shared<rows::BenchmarkProblemData>(
            rows::Problem{calendar_visits_, carers_, users_},
            time_horizon_,
            carer_used_penalty_,
            node_index_,
            visit_index_,
            distance_matrix_);
}

std::shared_ptr<rows::ProblemData> rows::BenchmarkProblemDataFactory::operator()(rows::Problem problem) const {
    std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, rows::CalendarVisit> node_index;
    std::unordered_map<rows::CalendarVisit,
            std::vector<operations_research::RoutingIndexManager::NodeIndex>,
            rows::Problem::PartialVisitOperations,
            rows::Problem::PartialVisitOperations> visit_index;

    operations_research::RoutingNodeIndex current_visit_node{1};
    for (const auto &visit : problem.visits()) {
        std::vector<operations_research::RoutingNodeIndex> node_indices;

        for (decltype(visit.carer_count()) visit_copy = 0; visit_copy < visit.carer_count(); ++visit_copy) {
            node_index.emplace(current_visit_node, visit);
            node_indices.emplace_back(current_visit_node);
            ++current_visit_node;
        }

        visit_index.emplace(visit, std::move(node_indices));
    }

    return std::make_shared<rows::BenchmarkProblemData>(problem,
                                                        time_horizon_,
                                                        carer_used_penalty_,
                                                        std::move(node_index),
                                                        std::move(visit_index),
                                                        distance_matrix_);
}

rows::BenchmarkProblemDataFactory rows::BenchmarkProblemDataFactory::Load(const std::string &file_path) {
    ampl::Environment env("/home/pmateusz/Applications/ampl.linux64");
    ampl::AMPL ampl{env};
    ampl.eval("param NO_Staff;"
              "param NO_Visits;"
              "param nModeOfTravel;"
              "param T_MAX;"
              "param extra_staff_penalty;"
              "set Visit := 1..NO_Visits;"
              "set Staff := 1..NO_Staff;"
              "set DepotVisit := 0..(NO_Visits + 1);"
              "set ModeOfTravel := 1..nModeOfTravel;"
              "set Visit_Demands{Visit};"
              "param a{Visit};"
              "param b{Visit};"
              "param Duration{Visit};"
              "param TimeMatrix{DepotVisit,DepotVisit,ModeOfTravel};"
              "param BonusMatrix{Staff,Visit};");
    ampl.readData(file_path);

    const int64 T_max = ampl.getParameter("T_MAX").get().dbl();
    const int64 extra_staff_penalty = ampl.getParameter("extra_staff_penalty").get().dbl();

    const auto visits = MemberRangeToVector(ampl.getSet("Visit").members());
    const auto staff = MemberRangeToVector(ampl.getSet("Staff").members());

    std::unordered_map<int, int> synchronised_visits;
    for (const auto visit : visits) {
        const auto visit_demands = ampl.getSet("Visit_Demands").get(ampl::Tuple(visit)).members();
        if (visit_demands.begin() != visit_demands.end()) {
            synchronised_visits.emplace(visit, (*visit_demands.begin())[0].dbl());
        }
    }

    const auto time_window_open = DataFrameToMap(ampl.getParameter("a").getValues(), 0, 1);
    const auto time_window_close = DataFrameToMap(ampl.getParameter("b").getValues(), 0, 1);
    const auto duration = DataFrameToMap(ampl.getParameter("Duration").getValues(), 0, 1);

    const auto today = boost::gregorian::day_clock::local_day();

    std::unordered_set<int> processed_nodes;
    std::vector<rows::ExtendedServiceUser> users;
    std::vector<rows::CalendarVisit> calendar_visits;
    std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, rows::CalendarVisit> node_index;
    std::unordered_map<rows::CalendarVisit,
            std::vector<operations_research::RoutingIndexManager::NodeIndex>,
            rows::Problem::PartialVisitOperations,
            rows::Problem::PartialVisitOperations> visit_index;

    for (const auto visit_node : visits) {
        if (processed_nodes.find(visit_node) != std::cend(processed_nodes)) {
            continue;
        }

        std::vector<operations_research::RoutingIndexManager::NodeIndex> local_visit_nodes;
        const auto synchronised_visit_it = synchronised_visits.find(visit_node);
        if (synchronised_visit_it != std::cend(synchronised_visits)) {
            local_visit_nodes.emplace_back(synchronised_visit_it->first);
            local_visit_nodes.emplace_back(synchronised_visit_it->second);
        } else {
            local_visit_nodes.emplace_back(visit_node);
        }

        const auto visit_window_open = time_window_open.at(visit_node);
        const auto visit_window_close = time_window_close.at(visit_node);
        boost::posix_time::time_period visit_time_windows{
                boost::posix_time::ptime{today, boost::posix_time::minutes(visit_window_open)},
                boost::posix_time::ptime{today, boost::posix_time::minutes(visit_window_close)}};

        const rows::Address address{};
        const rows::Location location{};
        const rows::ServiceUser user{std::to_string(visit_node)};
        const rows::CalendarVisit visit{static_cast<std::size_t>(visit_node),
                                        user,
                                        address,
                                        boost::none,
                                        visit_time_windows,
                                        boost::posix_time::minutes(duration.at(visit_node)),
                                        static_cast<int>(local_visit_nodes.size()),
                                        std::vector<int>{}};

        calendar_visits.emplace_back(visit);
        users.emplace_back(user.id(), address, location);

        for (const auto local_visit_node : local_visit_nodes) {
            node_index.emplace(local_visit_node, visit);
            processed_nodes.emplace(local_visit_node.value());
        }
        visit_index.emplace(visit, std::move(local_visit_nodes));
    }

    std::vector<std::pair<rows::Carer, std::vector<rows::Diary >>> carers;
    for (const auto staff_node : staff) {
        const boost::posix_time::time_period event{boost::posix_time::ptime{today}, boost::posix_time::minutes(T_max)};
        carers.emplace_back(std::make_pair(rows::Carer{std::to_string(staff_node)},
                                           std::vector<rows::Diary>{rows::Diary{today, std::vector<rows::Event>{rows::Event{event}}}}));
    }

    std::vector<std::vector<int>> distance_matrix(visits.size() + 2, std::vector<int>(visits.size() + 2, 0));
    const auto time_matrix_data_frame = ampl.getParameter("TimeMatrix").getValues();
    const auto num_rows = time_matrix_data_frame.getNumRows();
    for (decltype(time_matrix_data_frame.getNumRows()) row_index = 0; row_index < num_rows; ++row_index) {
        const auto row = time_matrix_data_frame.getRowByIndex(row_index);
        const auto values = RowToVector(row);
        CHECK_EQ(values.size(), 4);
        CHECK_EQ(values.at(2), 1);

        distance_matrix.at(values.at(0)).at(values.at(1)) = boost::posix_time::minutes(values.at(3)).total_seconds(); // convert minutes to seconds
    }

    return {std::move(users),
            std::move(calendar_visits),
            std::move(carers),
            boost::posix_time::time_period{boost::posix_time::ptime{today}, boost::posix_time::minutes(T_max)},
            extra_staff_penalty,
            std::move(node_index),
            std::move(visit_index),
            std::move(distance_matrix)};
}

rows::BenchmarkProblemDataFactory::BenchmarkProblemDataFactory(std::vector<rows::ExtendedServiceUser> users,
                                                               std::vector<rows::CalendarVisit> calendar_visits,
                                                               std::vector<std::pair<rows::Carer, std::vector<rows::Diary>>> carers,
                                                               boost::posix_time::time_period time_horizon,
                                                               int64 carer_used_penalty,
                                                               std::unordered_map<operations_research::RoutingIndexManager::NodeIndex, rows::CalendarVisit> node_index,
                                                               std::unordered_map<rows::CalendarVisit,
                                                                       std::vector<operations_research::RoutingIndexManager::NodeIndex>,
                                                                       rows::Problem::PartialVisitOperations,
                                                                       rows::Problem::PartialVisitOperations> visit_index,
                                                               std::vector<std::vector<int>> distance_matrix)
        : users_{std::move(users)},
          calendar_visits_{std::move(calendar_visits)},
          carers_{std::move(carers)},
          time_horizon_{time_horizon},
          carer_used_penalty_{carer_used_penalty},
          node_index_{std::move(node_index)},
          visit_index_{std::move(visit_index)},
          distance_matrix_{std::move(distance_matrix)} {}
