// scheduler.hpp
#pragma once
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_set>

class Scheduler final {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Ms        = std::chrono::milliseconds;
    using Fn        = std::function<void()>;
    using TaskId    = std::uint64_t;

    // Singleton access
    static Scheduler& instance(std::size_t poolThreads = std::thread::hardware_concurrency()) {
        static Scheduler inst(poolThreads == 0 ? 1 : poolThreads);
        return inst;
    }

    // One-shot (delayed) task
    TaskId addDelayed(Fn fn, Ms delay) {
        return addTask(std::move(fn), Clock::now() + delay, Ms::zero(), /*periodic=*/false);
    }

    // Periodic task (runs first time after 'period', затем каждые 'period')
    TaskId addPeriodic(Fn fn, Ms period) {
        if (period.count() <= 0) period = Ms(1);
        return addTask(std::move(fn), Clock::now() + period, period, /*periodic=*/true);
    }

    // Cancel by id (best-effort; если задача уже исполняется — не прервёт)
    bool cancel(TaskId id) {
        std::lock_guard<std::mutex> lk(mtx_);
        return cancelSet_.insert(id).second; // пометили как отменённую
    }

    // Graceful stop (ждёт завершения диспетчерского потока, гасит пул)
    void stop() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stopped_) return;
            stopped_ = true;
        }
        cv_.notify_all();
        if (dispatcher_.joinable()) dispatcher_.join();
        pool_.join(); // дождаться всех работ пула
    }

    // Запрещаем копии
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

private:
    struct Item {
        TimePoint when;
        TaskId    id;
        Fn        fn;
        Ms        period;
        bool      periodic;

        // priority_queue по умолчанию — max-heap, поэтому инвертируем сравнение:
        bool operator<(Item const& other) const noexcept {
            return when > other.when; // "раньше" == "выше" приоритета
        }
    };

    explicit Scheduler(std::size_t threads)
        : pool_(threads), dispatcher_(&Scheduler::loop, this) {}

    TaskId addTask(Fn fn, TimePoint when, Ms period, bool periodic) {
        Item it{when, nextId_++, std::move(fn), period, periodic};
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stopped_) return invalidId();
            pq_.push(std::move(it));
        }
        cv_.notify_all();
        return it.id;
    }

    static TaskId invalidId() { return 0; }

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
                continue; // проверим заново условия
            }

            // Достаем задачу к исполнению
            pq_.pop();

            // Проверка на отмену
            if (cancelSet_.erase(next.id) > 0) {
                // Если периодическая и отменена — не пере-планируем
                continue;
            }

            // Постим задачу в пул
            auto id_copy = next.id;
            auto fn_copy = next.fn; // скопировали, чтобы освободить мьютекс быстро
            lk.unlock();
            boost::asio::post(pool_, [fn = std::move(fn_copy), id_copy]() mutable {
                try { fn(); } catch (...) { /* логирование по желанию */ }
            });
            lk.lock();

            // Если периодическая — перепланировать
            if (next.periodic && cancelSet_.count(id_copy) == 0 && !stopped_) {
                next.when = Clock::now() + next.period; // дрейф: фикс запуска «от последнего» (can switch to fixed-rate если нужно)
                pq_.push(std::move(next));
            }
        }
    }

private:
    boost::asio::thread_pool pool_;
    std::thread              dispatcher_;

    std::mutex               mtx_;
    std::condition_variable  cv_;
    std::priority_queue<Item> pq_;
    std::unordered_set<TaskId> cancelSet_;
    std::atomic<TaskId>      nextId_{1};
    bool                     stopped_{false};
};