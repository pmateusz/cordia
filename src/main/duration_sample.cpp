#include "duration_sample.h"


rows::DurationSample::DurationSample(const rows::Problem &problem,
                                     const rows::SolverWrapper &solver,
                                     const rows::History &history,
                                     operations_research::RoutingModel &model) {
    for (const auto &visit : solver.problem().visits()) {
    }
}
