#include "ui/worker.h"

namespace srp::ui {

Worker::~Worker() { Stop(); }

void Worker::Post(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        queue_.push_back(std::move(task));
        if (!started_) {
            started_ = true;
            thread_ = std::thread([this] { Run(); });
        }
    }
    cv_.notify_one();
}

void Worker::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!started_) return;
        stopping_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    started_ = false;
}

bool Worker::Idle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty() && !busy_;
}

void Worker::Run() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            if (stopping_) return;
            task = std::move(queue_.front());
            queue_.pop_front();
            busy_ = true;
        }
        task();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
    }
}

}  // namespace srp::ui
