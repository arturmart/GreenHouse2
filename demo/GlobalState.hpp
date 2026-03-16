#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <chrono>
#include <stdexcept>
#include <utility>

enum class GH_MODE : uint8_t { MANUAL = 0, AUTO = 1 };

inline std::string toString(GH_MODE m) {
    return (m == GH_MODE::MANUAL) ? "MANUAL" : "AUTO";
}

class GH_GlobalState final {
public:
    // ------------------------------------------------------------
    // Value type
    // ------------------------------------------------------------
    enum class ValueType : uint8_t {
        BOOL,
        INT,
        DOUBLE,
        STRING
    };

    // ------------------------------------------------------------
    // Getter entry
    // ------------------------------------------------------------
    struct GetterEntry {
        std::any value;
        bool valid{false};
        uint64_t stampMs{0};
    };

    // ------------------------------------------------------------
    // Executor entries
    // ------------------------------------------------------------
    struct ExecEntry {
        std::any value;
        GH_MODE mode{GH_MODE::MANUAL};
        bool valid{true};
        uint64_t stampMs{0};
    };

    struct ExecDesiredEntry {
        std::any value;
        GH_MODE mode{GH_MODE::MANUAL};
        bool valid{false};
        bool dirty{false};
        std::string lastWriter{"unknown"};
        uint64_t stampMs{0};
    };

    struct ExecActualEntry {
        std::any value;
        GH_MODE mode{GH_MODE::MANUAL};
        bool valid{false};
        bool pending{false};
        std::string lastError{};
        uint64_t stampMs{0};
        uint64_t lastAppliedMs{0};
    };

    struct ExecFullEntry {
        ExecDesiredEntry desired;
        ExecActualEntry actual;
    };

    // ------------------------------------------------------------
    // DCM binding
    // ------------------------------------------------------------
    struct DcmBinding {
        int tableId{0};
        int index{0};
        ValueType type{ValueType::BOOL};
    };

    // ------------------------------------------------------------
    // Type aliases
    // ------------------------------------------------------------
    using NameToId         = std::unordered_map<std::string, int>;
    using GetterMap        = std::unordered_map<std::string, GetterEntry>;
    using GetterSchema     = std::unordered_map<std::string, ValueType>;
    using ExecSchemaByName = std::unordered_map<std::string, ValueType>;
    using DcmBindingMap    = std::unordered_map<std::string, DcmBinding>;
    using ExecMap          = std::unordered_map<int, ExecFullEntry>;

    // ------------------------------------------------------------
    // Singleton
    // ------------------------------------------------------------
    static GH_GlobalState& instance() {
        static GH_GlobalState inst;
        return inst;
    }

    GH_GlobalState(const GH_GlobalState&) = delete;
    GH_GlobalState& operator=(const GH_GlobalState&) = delete;

    // ------------------------------------------------------------
    // Time
    // ------------------------------------------------------------
    static uint64_t nowMs() {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<milliseconds>(
                steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    // ------------------------------------------------------------
    // Schema registration
    // ------------------------------------------------------------
    void setGetterSchema(const std::string& key, ValueType t) {
        std::unique_lock lk(schema_mtx_);
        getter_schema_[key] = t;
    }

    void setExecSchemaByName(const std::string& name, ValueType t) {
        std::unique_lock lk(schema_mtx_);
        exec_schema_by_name_[name] = t;
    }

    void setDcmBindingByName(const std::string& name, DcmBinding b) {
        std::unique_lock lk(schema_mtx_);
        dcm_bindings_[name] = std::move(b);
    }

    void registerExecNameToId(const std::string& name, int id) {
        std::unique_lock lk(exec_mtx_);
        exec_name_to_id_[name] = id;
    }

    // ------------------------------------------------------------
    // Getter read helpers
    // ------------------------------------------------------------
    template<class T>
    T getGetterAs(const std::string& key) const {
        std::shared_lock lk(getter_mtx_);

        auto it = getter_status_.find(key);
        if (it == getter_status_.end())
            throw std::runtime_error("Getter key not found: " + key);

        if (!it->second.valid)
            throw std::runtime_error("Getter key invalid: " + key);

        return std::any_cast<T>(it->second.value);
    }

    GetterEntry getGetterEntry(const std::string& key) const {
        std::shared_lock lk(getter_mtx_);

        auto it = getter_status_.find(key);
        if (it == getter_status_.end())
            throw std::runtime_error("Getter key not found: " + key);

        return it->second;
    }

    // ------------------------------------------------------------
    // Executor name/id helpers
    // ------------------------------------------------------------
    int execIdByName(const std::string& name) const {
        std::shared_lock lk(exec_mtx_);

        auto it = exec_name_to_id_.find(name);
        if (it == exec_name_to_id_.end())
            throw std::runtime_error("Executor name not found: " + name);

        return it->second;
    }

    DcmBinding getDcmBindingByName(const std::string& name) const {
        std::shared_lock lk(schema_mtx_);

        auto it = dcm_bindings_.find(name);
        if (it == dcm_bindings_.end())
            throw std::runtime_error("DCM binding not found for executor: " + name);

        return it->second;
    }

    // ------------------------------------------------------------
    // Executor read helpers (NEW)
    // ------------------------------------------------------------
    ExecFullEntry getExecFullEntry(int id) const {
        std::shared_lock lk(exec_mtx_);

        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found");

        return it->second;
    }

    ExecDesiredEntry getExecDesiredEntry(int id) const {
        std::shared_lock lk(exec_mtx_);

        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found");

        return it->second.desired;
    }

    ExecActualEntry getExecActualEntry(int id) const {
        std::shared_lock lk(exec_mtx_);

        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found");

        return it->second.actual;
    }

    bool isExecDirty(int id) const {
        std::shared_lock lk(exec_mtx_);

        auto it = executor_status_.find(id);
        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found");

        return it->second.desired.dirty;
    }

    // ------------------------------------------------------------
    // Compatibility read helpers (OLD API -> actual state)
    // ------------------------------------------------------------
    ExecEntry getExecEntry(int id) const {
        auto a = getExecActualEntry(id);

        ExecEntry e;
        e.value = a.value;
        e.mode = a.mode;
        e.valid = a.valid;
        e.stampMs = a.stampMs;
        return e;
    }

    template<class T>
    T getExecValueAs(int id) const {
        auto a = getExecActualEntry(id);

        if (!a.valid)
            throw std::runtime_error("Executor value invalid");

        return std::any_cast<T>(a.value);
    }

    GH_MODE getExecMode(int id) const {
        return getExecActualEntry(id).mode;
    }

    // ------------------------------------------------------------
    // Snapshots
    // ------------------------------------------------------------
    GetterSchema snapshotGetterSchema() const {
        std::shared_lock lk(schema_mtx_);
        return getter_schema_;
    }

    ExecSchemaByName snapshotExecSchemaByName() const {
        std::shared_lock lk(schema_mtx_);
        return exec_schema_by_name_;
    }

    DcmBindingMap snapshotDcmBindings() const {
        std::shared_lock lk(schema_mtx_);
        return dcm_bindings_;
    }

    GetterMap snapshotGetters() const {
        std::shared_lock lk(getter_mtx_);
        return getter_status_;
    }

    struct ExecApiEntry {
        int id{0};
        std::string name;
        ExecEntry entry;               // compatibility actual state
        ExecDesiredEntry desired;      // new
        ExecActualEntry actual;        // new
    };

    std::vector<ExecApiEntry> snapshotExecutors() const {
        std::shared_lock lk(exec_mtx_);

        std::unordered_map<int, std::string> id2name;
        for (const auto& kv : exec_name_to_id_) {
            id2name[kv.second] = kv.first;
        }

        std::vector<ExecApiEntry> out;
        out.reserve(executor_status_.size());

        for (const auto& kv : executor_status_) {
            ExecApiEntry e;
            e.id = kv.first;

            auto it = id2name.find(kv.first);
            if (it != id2name.end())
                e.name = it->second;

            e.desired = kv.second.desired;
            e.actual = kv.second.actual;

            e.entry.value = kv.second.actual.value;
            e.entry.mode = kv.second.actual.mode;
            e.entry.valid = kv.second.actual.valid;
            e.entry.stampMs = kv.second.actual.stampMs;

            out.push_back(std::move(e));
        }

        return out;
    }

    // ------------------------------------------------------------
    // Getter write helpers
    // ------------------------------------------------------------
    void setGetter(const std::string& key, std::any value) {
        std::unique_lock lk(getter_mtx_);

        auto& e = getter_status_[key];
        e.value = std::move(value);
        e.valid = true;
        e.stampMs = nowMs();
    }

    void setGetterInvalid(const std::string& key) {
        std::unique_lock lk(getter_mtx_);

        auto& e = getter_status_[key];
        e.valid = false;
        e.stampMs = nowMs();
    }

    // ------------------------------------------------------------
    // Executor desired write helpers (NEW)
    // ------------------------------------------------------------
    void setExecDesired(int id, std::any value, GH_MODE mode,
                        std::string writer = "unknown",
                        bool dirty = true) {
        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];
        e.desired.value = std::move(value);
        e.desired.mode = mode;
        e.desired.valid = true;
        e.desired.dirty = dirty;
        e.desired.lastWriter = std::move(writer);
        e.desired.stampMs = nowMs();
    }

    void setExecDesiredInvalid(int id, std::string writer = "unknown", bool dirty = true) {
        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];
        e.desired.valid = false;
        e.desired.dirty = dirty;
        e.desired.lastWriter = std::move(writer);
        e.desired.stampMs = nowMs();
    }

    void setExecDesiredMode(int id, GH_MODE mode, std::string writer = "unknown", bool dirty = true) {
        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];
        e.desired.mode = mode;
        e.desired.dirty = dirty;
        e.desired.lastWriter = std::move(writer);
        e.desired.stampMs = nowMs();
    }

    void markExecDirty(int id, bool dirty = true) {
        std::unique_lock lk(exec_mtx_);
        executor_status_[id].desired.dirty = dirty;
    }

    // ------------------------------------------------------------
    // Executor actual write helpers (NEW)
    // ------------------------------------------------------------
    void setExecActual(int id, std::any value, GH_MODE mode, bool pending = false) {
        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];
        e.actual.value = std::move(value);
        e.actual.mode = mode;
        e.actual.valid = true;
        e.actual.pending = pending;
        e.actual.lastError.clear();
        e.actual.stampMs = nowMs();
        e.actual.lastAppliedMs = e.actual.stampMs;
    }

    void setExecActualInvalid(int id, std::string err = {}) {
        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];
        e.actual.valid = false;
        e.actual.pending = false;
        e.actual.lastError = std::move(err);
        e.actual.stampMs = nowMs();
    }

    void setExecActualMode(int id, GH_MODE mode) {
        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];
        e.actual.mode = mode;
        e.actual.stampMs = nowMs();
    }

    void setExecPending(int id, bool pending) {
        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];
        e.actual.pending = pending;
        e.actual.stampMs = nowMs();
    }

    void setExecApplyError(int id, const std::string& err) {
        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];
        e.actual.lastError = err;
        e.actual.pending = false;
        e.actual.stampMs = nowMs();
    }

    void clearExecApplyError(int id) {
        std::unique_lock lk(exec_mtx_);
        executor_status_[id].actual.lastError.clear();
    }

    // ------------------------------------------------------------
    // Compatibility write helpers (OLD API -> actual state only)
    // ------------------------------------------------------------
    void setExec(int id, std::any value, GH_MODE mode) {
        setExecActual(id, std::move(value), mode, false);
    }

    void setExecInvalid(int id) {
        setExecActualInvalid(id);
    }

    void setExecMode(int id, GH_MODE mode) {
        setExecActualMode(id, mode);
    }

private:
    GH_GlobalState() = default;

    mutable std::shared_mutex exec_mtx_;
    mutable std::shared_mutex getter_mtx_;
    mutable std::shared_mutex schema_mtx_;

    ExecMap executor_status_;
    NameToId exec_name_to_id_;
    GetterMap getter_status_;

    GetterSchema getter_schema_;
    ExecSchemaByName exec_schema_by_name_;
    DcmBindingMap dcm_bindings_;
};