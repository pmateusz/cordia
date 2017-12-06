#include "problem.h"

#include <boost/format.hpp>

namespace rows {

    Problem::Problem(std::vector<Visit> visits, std::unordered_map<Carer, std::vector<Diary> > carers)
            : visits_(std::move(visits)),
              carers_(std::move(carers)) {}

    const std::vector<Visit> &Problem::visits() const {
        return visits_;
    }

    const std::unordered_map<Carer, std::vector<Diary> > &Problem::carers() const {
        return carers_;
    }

    std::domain_error Problem::JsonLoader::OnKeyNotFound(std::string key) {
        return std::domain_error((boost::format("Key '%1%' not found") % key).str());
    }
}