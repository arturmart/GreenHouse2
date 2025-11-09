#pragma once
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class Scheduler final {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Ms        = std::chrono::milliseconds;
    using Fn        = std::function<void()>;
    using TaskId    = std::uint64_t;

    struct TaskInfo {
        TaskId     id;
        std::string name;
        bool       periodic;
        long long  msUntilRun;
        long long  periodMs;
    };
    struct RunningInfo {
        TaskId      id;
        std::string name;
        int         workerIndex;   // стабильный индекс потока (W0, W1, ...)
    };

    static Scheduler& instance(std::size_t poolThreads = std::thread::hardware_concurrency()) {
        static Scheduler inst(poolThreads == 0 ? 1 : poolThreads);
        return inst;
    }

    TaskId addDelayed(Fn fn, Ms delay, std::string name = "") {
        return addTask(std::move(fn), Clock::now() + delay, Ms::zero(), false, std::move(name));
    }
    TaskId addPeriodic(Fn fn, Ms period, std::string name = "") {
        if (period.count() <= 0) period = Ms(1);
        return addTask(std::move(fn), Clock::now() + period, period, true, std::move(name));
    }
    bool cancel(TaskId id) {
        std::lock_guard<std::mutex> lk(mtx_);
        return cancelSet_.insert(id).second;
    }
    void stop() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stopped_) return;
            stopped_ = true;
        }
        cv_.notify_all();
        if (dispatcher_.joinable()) dispatcher_.join();
        pool_.join();
    }

    // Печать очереди
    void debugDump() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::cout << "\n=== Scheduler Debug Dump ===\n";
        std::cout << "Queued tasks: " << pq_.size() << "\n";
        std::cout << "Running:      " << running_.size() << "\n";
        std::cout << "Canceled:     " << cancelSet_.size() << "\n";
        std::cout << "Stopped:      " << std::boolalpha << stopped_ << "\n";
        auto copy = pq_;
        auto now = Clock::now();
        while (!copy.empty()) {
            const auto& it = copy.top();
            auto ms_left = std::chrono::duration_cast<Ms>(it.when - now).count();
            std::cout << "  id=" << it.id
                      << " name=\"" << it.name << "\""
                      << " periodic=" << it.periodic
                      << " in=" << ms_left << " ms"
                      << " period=" << it.period.count() << " ms\n";
            copy.pop();
        }
        std::cout << "=============================\n";
    }

    // Списки для программного использования
    std::vector<TaskInfo> listTasks() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<TaskInfo> result;
        result.reserve(pq_.size());
        auto copy = pq_;
        auto now = Clock::now();
        while (!copy.empty()) {
            const auto& it = copy.top();
            long long ms_left = std::chrono::duration_cast<Ms>(it.when - now).count();
            result.push_back(TaskInfo{it.id, it.name, it.periodic, ms_left, it.period.count()});
            copy.pop();
        }
        return result;
    }
    std::vector<RunningInfo> listRunningDetailed() {
        std::lock_guard<std::mutex> lk(mtx_);
        std::vector<RunningInfo> v;
        v.reserve(running_.size());
        for (const auto& kv : running_) {
            const TaskId id = kv.first;
            const auto&   meta = kv.second; // {tid, name}
            int idx = ensureWorkerIndexUnlocked(meta.tid);
            v.push_back(RunningInfo{ id, meta.name, idx });
        }
        return v;
    }
    int workersObserved() {
        std::lock_guard<std::mutex> lk(mtx_);
        return (int)workerIndex_.size();
    }

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

private:
    struct Item {
        TimePoint   when;
        TaskId      id;
        Fn          fn;
        Ms          period;
        bool        periodic;
        std::string name;
        bool operator<(Item const& other) const noexcept {
            return when > other.when;
        }
    };
    struct RunningMeta {
        std::thread::id tid;
        std::string     name;
    };

    explicit Scheduler(std::size_t threads)
        : pool_(threads), dispatcher_(&Scheduler::loop, this) {}

    static TaskId invalidId() { return 0; }

    TaskId addTask(Fn fn, TimePoint when, Ms period, bool periodic, std::string name) {
        Item it{when, nextId_++, std::move(fn), period, periodic, std::move(name)};
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stopped_) return invalidId();
            pq_.push(std::move(it));
        }
        cv_.notify_all();
        return it.id;
    }

    void markRunning(TaskId id, const std::string& name, const std::thread::id& tid) {
        std::lock_guard<std::mutex> lk(mtx_);
        running_[id] = RunningMeta{tid, name};
        (void)ensureWorkerIndexUnlocked(tid); // присвоим индекс если новый поток
    }
    void unmarkRunning(TaskId id) {
        std::lock_guard<std::mutex> lk(mtx_);
        running_.erase(id);
    }
    int ensureWorkerIndexUnlocked(const std::thread::id& tid) {
        auto it = workerIndex_.find(tid);
        if (it != workerIndex_.end()) return it->second;
        int idx = (int)workerIndex_.size();
        workerIndex_[tid] = idx;
        return idx;
    }

    void loop() {
        std::unique_lock<std::mutex> lk(mtx_);
        for (;;) {
            if (stopped_) break;

            if (pq_.empty()) {
                cv_.wait(lk, [&]{ return stopped_ || !pq_.empty(); });
                if (stopped_) break;
            }

            auto now = Clock::now();
            auto next = pq_.top();

            if (next.when > now) {
                cv_.wait_until(lk, next.when, [&]{
                    return stopped_ || pq_.empty() || pq_.top().when != next.when;
                });
                continue;
            }

            pq_.pop();

            if (cancelSet_.erase(next.id) > 0) {
                continue;
            }

            auto id_copy   = next.id;
            auto fn_copy   = next.fn;
            auto name_copy = next.name;

            lk.unlock();
            boost::asio::post(pool_, [this, fn = std::move(fn_copy), id_copy, name_copy]() mutable {
                auto tid = std::this_thread::get_id();
                markRunning(id_copy, name_copy, tid);
                try { fn(); } catch (...) { /* логируйте при необходимости */ }
                unmarkRunning(id_copy);
            });
            lk.lock();

            if (next.periodic && cancelSet_.count(id_copy) == 0 && !stopped_) {
                next.when = Clock::now() + next.period; // fixed-delay
                pq_.push(std::move(next));
            }
        }
    }

private:
    boost::asio::thread_pool   pool_;
    std::thread                dispatcher_;
    std::mutex                 mtx_;
    std::condition_variable    cv_;
    std::priority_queue<Item>  pq_;
    std::unordered_set<TaskId> cancelSet_;

    // running_[taskId] -> {thread_id, name}
    std::unordered_map<TaskId, RunningMeta> running_;
    // thread_id -> stable index (W0, W1, ...)
    std::unordered_map<std::thread::id, int> workerIndex_;

    std::atomic<TaskId>        nextId_{1};
    bool                       stopped_{false};
};
