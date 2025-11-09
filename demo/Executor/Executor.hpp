#pragma once
#include <unordered_map>
#include <queue>
#include <memory>
#include <any>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>
#include "AExecutor_Strategy.hpp"

namespace exec {

class Executor {
public:
    using Args = AExecutorStrategy::Args;
    using Ctx  = AExecutorStrategy::Ctx;
    using StrategyUP = std::unique_ptr<AExecutorStrategy>;

    // --- команды ---
    bool registerCommand(const std::string& key, StrategyUP strat) {
        if (!strat) return false;
        return commands_.emplace(key, std::move(strat)).second;
    }
    bool hasCommand(const std::string& key) const { return commands_.find(key) != commands_.end(); }
    bool removeCommand(const std::string& key) { return commands_.erase(key) > 0; }

    // --- init конкретной команды (унифицированно) ---
    void initCommand(const std::string& key, const Ctx& ctx) {
        auto it = commands_.find(key);
        if (it != commands_.end() && it->second) {
            it->second->init(ctx);
        }
    }
    // перегрузка с вариадиком (ключ-значение парами): initCommand("SIM_HEAT", "sim", (ISimControl*)ptr);
    template <class... Ts>
    void initCommandKV(const std::string& key, Ts&&... kv_pairs) {
        static_assert(sizeof...(kv_pairs) % 2 == 0, "initCommandKV expects even number of kv args");
        Ctx ctx;
        fillKV(ctx, std::forward<Ts>(kv_pairs)...);
        initCommand(key, ctx);
    }

    // --- init всех команд общим контекстом ---
    void initAll(const Ctx& ctx) {
        for (auto& kv : commands_) {
            if (kv.second) kv.second->init(ctx);
        }
    }

    // --- очередь задач ---
    void enqueue(const std::string& key, int priority, const Args& args) { pushTask(key, priority, args); }
    void enqueue(const std::string& key, int priority, Args&& args)      { pushTask(key, priority, std::move(args)); }

    template<class... Ts>
    void enqueue(const std::string& key, int priority, Ts&&... ts) {
        Args a; a.reserve(sizeof...(Ts));
        (a.emplace_back(std::forward<Ts>(ts)), ...);
        pushTask(key, priority, std::move(a));
    }

    // --- tick(): одна задача ---
    bool tick() {
        if (pq_.empty()) return false;
        Task task = pq_.top(); pq_.pop();

        auto it = commands_.find(task.key);
        if (it == commands_.end()) {
            std::cout << "[Executor] command not found: " << task.key << "\n";
            return false;
        }
        try {
            it->second->execute(task.args);
        } catch (...) {
            std::cout << "[Executor] error executing '" << task.key << "'\n";
        }
        return true;
    }

    void tickStrategies() {
        for (auto& kv : commands_) {
            if (kv.second) {
                try { kv.second->tick(); }
                catch (...) { std::cout << "[Executor] tick() failed for " << kv.first << "\n"; }
            }
        }
    }

    size_t queued() const { return pq_.size(); }

private:
    struct Task {
        std::string key;
        Args        args;
        int         priority;
        std::uint64_t seq;
    };
    struct TaskCmp {
        bool operator()(const Task& a, const Task& b) const noexcept {
            if (a.priority != b.priority) return a.priority < b.priority; // выше приоритет — раньше
            return a.seq > b.seq; // FIFO при равенстве
        }
    };

    void pushTask(const std::string& key, int priority, const Args& args) {
        Task t{key, args, priority, seq_++};
        pq_.push(std::move(t));
    }
    void pushTask(const std::string& key, int priority, Args&& args) {
        Task t{key, std::move(args), priority, seq_++};
        pq_.push(std::move(t));
    }

    // helper для initCommandKV
    static void fillKV(Ctx&) {}
    template <class K, class V, class... Rest>
    static void fillKV(Ctx& ctx, K&& k, V&& v, Rest&&... rest) {
        ctx.emplace(std::string(std::forward<K>(k)), std::any(std::forward<V>(v)));
        if constexpr (sizeof...(Rest) > 0) fillKV(ctx, std::forward<Rest>(rest)...);
    }

private:
    std::unordered_map<std::string, StrategyUP> commands_;
    std::priority_queue<Task, std::vector<Task>, TaskCmp> pq_;
    std::uint64_t seq_{0};
};

} // namespace exec
