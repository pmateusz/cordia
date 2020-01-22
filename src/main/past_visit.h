#ifndef ROWS_PAST_VISIT_H
#define ROWS_PAST_VISIT_H

#include <nlohmann/json.hpp>

namespace rows {

    class PastVisit {
    public:
        PastVisit() = default;
    };

    void to_json(nlohmann::json &json, const PastVisit &solution);

    void from_json(const nlohmann::json &json, PastVisit &solution);
}


#endif //ROWS_PAST_VISIT_H
