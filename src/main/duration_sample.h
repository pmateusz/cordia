#ifndef ROWS_DURATION_SAMPLE_H
#define ROWS_DURATION_SAMPLE_H


#include <ortools/constraint_solver/routing.h>

#include "history.h"
#include "problem_data.h"
#include "solver_wrapper.h"

namespace rows {

    class DurationSample {
    public:
        DurationSample(const Problem &problem, const SolverWrapper &solver, const History &history, operations_research::RoutingModel &model);
    };
}


#endif //ROWS_DURATION_SAMPLE_H
