#include "problem.h"

#include <boost/format.hpp>

#include <glog/logging.h>

namespace rows {

    Problem::Problem(std::vector<Visit> visits, std::vector<std::pair<Carer, std::vector<Diary> > > carers)
            : visits_(std::move(visits)),
              carers_(std::move(carers)) {}

    const std::vector<Visit> &Problem::visits() const {
        return visits_;
    }

    const std::vector<std::pair<Carer, std::vector<Diary> > > &Problem::carers() const {
        return carers_;
    }

    bool Problem::IsAdmissible() const {
        auto required_duration_sec = 0;
        for (const auto &visit : visits_) {
            required_duration_sec += visit.duration().total_seconds();
        }

        auto available_duration_sec = 0;
        for (const auto &carer_diary_pair : carers_) {
            for (const auto &diary : carer_diary_pair.second) {
                for (const auto &event : diary.events()) {
                    DCHECK_LE(event.begin(), event.end());

                    available_duration_sec += (event.end() - event.begin()).total_seconds();
                }
            }
        }

        return required_duration_sec <= available_duration_sec;
    }

    std::domain_error Problem::JsonLoader::OnKeyNotFound(std::string key) {
        return std::domain_error((boost::format("Key '%1%' not found") % key).str());
    }
}