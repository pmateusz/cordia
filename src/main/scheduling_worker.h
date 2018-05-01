#ifndef ROWS_SCHEDULING_WORKER_H
#define ROWS_SCHEDULING_WORKER_H

#include <memory>
#include <thread>
#include <atomic>
#include <ortools/constraint_solver/routing.h>

namespace rows {

    class SchedulingWorker {
    public:
        static const int NOT_STARTED = -1;

        static const int STATUS_OK = 1;

        SchedulingWorker();

        virtual ~SchedulingWorker() = default;

        virtual void Run() = 0;

        void Start();

        void Join();

        void Cancel();

        int ReturnCode() const;

    protected:
        std::shared_ptr<const std::atomic<bool> > CancelToken() const;

        void ResetCancelToken();

        void SetReturnCode(int return_code);

    private:
        std::atomic<int> return_code_;
        std::shared_ptr<std::atomic<bool>> cancel_token_;
        std::thread worker_{};
    };
}

#endif //ROWS_SCHEDULING_WORKER_H
