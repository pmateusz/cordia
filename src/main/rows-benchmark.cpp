#include <cstdlib>

#include <ampl/ampl.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <boost/format.hpp>

#include "util/input.h"
#include "util/logging.h"
#include "util/validation.h"

DEFINE_string(problem, "../problem.json", "a file path to the problem instance");
DEFINE_validator(problem, &util::file::Exists);

DEFINE_string(output, "output.gexf", "an output file");

void ParseArgs(int argc, char *argv[]) {
    gflags::SetVersionString("0.0.1");
    gflags::SetUsageMessage("Robust Optimization for Workforce Scheduling\n"
                            "Example: rows-mip"
                            " --problem=problem.json"
                            " --maps=./data/scotland-latest.osrm");

    static const auto REMOVE_FLAGS = false;
    gflags::ParseCommandLineFlags(&argc, &argv, REMOVE_FLAGS);

    VLOG(1) << boost::format("Launched with the arguments:\n"
                             "problem: %1%") % FLAGS_problem;
}

std::vector<int> MemberRangeToVector(const ampl::Set::MemberRange &member_range) {
    std::vector<int> values;
    for (const auto &value : member_range) {
        values.push_back(value[0].dbl());
    }
    return values;
}

std::vector<int> RowToVector(const ampl::DataFrame::Row &row) {
    std::vector<int> values;
    for (decltype(row.size()) index = 0; index < row.size(); ++index) {
        values.emplace_back(row[index].dbl());
    }
    return values;
}

std::unordered_map<int, int> DataFrameToMap(const ampl::DataFrame &data_frame, std::size_t index_column, std::size_t series_column) {
    std::unordered_map<int, int> bundle;
    for (const auto &row: data_frame) {
        bundle.emplace(row[index_column].dbl(), row[series_column].dbl());
    }
    return bundle;
}

int main(int argc, char *argv[]) {
    util::SetupLogging(argv[0]);
    ParseArgs(argc, argv);

    ampl::Environment env("/home/pmateusz/Applications/ampl.linux64");
    ampl::AMPL ampl{env};
    ampl.eval("param NO_Staff;"
              "param NO_Visits;"
              "param nModeOfTravel;"
              "param T_MAX;"
              "param extra_staff_penalty;"
              "set Visit := 1..NO_Visits;"
              "set Staff := 1..NO_Staff;"
              "set ModeOfTravel := 1..nModeOfTravel;"
              "set Visit_Demands{Visit};"
              "param a{Visit};"
              "param b{Visit};"
              "param Duration{Visit};"
              "param TimeMatrix{Visit,Visit,ModeOfTravel};"
              "param BonusMatrix{Staff,Visit};");
    ampl.readData(FLAGS_problem);

    const auto T_max = ampl.getParameter("T_MAX").get().dbl();
    const auto extra_staff_penalty = ampl.getParameter("extra_staff_penalty").get().dbl();

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

    std::vector<std::vector<int>> distance_matrix(visits.size() + 1, std::vector<int>(visits.size() + 1, 0));

    const auto time_matrix_data_frame = ampl.getParameter("TimeMatrix").getValues();
    const auto num_rows = time_matrix_data_frame.getNumRows();
    for (decltype(time_matrix_data_frame.getNumRows()) row_index = 0; row_index < num_rows; ++row_index) {
        const auto row = time_matrix_data_frame.getRowByIndex(row_index);
        const auto values = RowToVector(row);
        CHECK_EQ(values.size(), 4);
        CHECK_EQ(values.at(2), 1);

        distance_matrix.at(values.at(0)).at(values.at(1)) = values.at(3);
    }

    return EXIT_SUCCESS;
}
