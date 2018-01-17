#include "solution.h"

rows::Solution::Solution(std::vector<rows::ScheduledVisit> visits)
        : visits_(std::move(visits)) {}

const std::vector<rows::ScheduledVisit> &rows::Solution::visits() const {
    return visits_;
}
