#include "scheduling_worker.h"

#include <glog/logging.h>

rows::SchedulingWorker::SchedulingWorker()
        : return_code_{NOT_STARTED},
          cancel_token_{std::make_shared<std::atomic<bool> >(false)},
          worker_{} {}

void rows::SchedulingWorker::Start() {
    worker_ = std::thread(&SchedulingWorker::Run, this);
}

void rows::SchedulingWorker::Join() {
    worker_.join();
}

void rows::SchedulingWorker::Cancel() {
    VLOG(1) << "Cancellation requested";
    cancel_token_->operator=(true);
}

int rows::SchedulingWorker::ReturnCode() const {
    return return_code_;
}

std::shared_ptr<const std::atomic<bool> > rows::SchedulingWorker::CancelToken() const {
    return cancel_token_;
}

void rows::SchedulingWorker::ResetCancelToken() {
    cancel_token_->operator=(false);
}

void rows::SchedulingWorker::SetReturnCode(int return_code) {
    return_code_ = return_code;
}
