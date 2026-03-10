#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <chrono>
#include <stdexcept>

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
    // Executor entry
    // ------------------------------------------------------------

    struct ExecEntry {
        std::any value;
        GH_MODE mode{GH_MODE::MANUAL};
        bool valid{true};
        uint64_t stampMs{0};
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

    using ExecMap          = std::unordered_map<int, ExecEntry>;
    using NameToId         = std::unordered_map<std::string, int>;
    using GetterMap        = std::unordered_map<std::string, GetterEntry>;
    using GetterSchema     = std::unordered_map<std::string, ValueType>;
    using ExecSchemaByName = std::unordered_map<std::string, ValueType>;
    using DcmBindingMap    = std::unordered_map<std::string, DcmBinding>;

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
    // Read helpers
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

    ExecEntry getExecEntry(int id) const {
        std::shared_lock lk(exec_mtx_);

        auto it = executor_status_.find(id);

        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found");

        return it->second;
    }

    template<class T>
    T getExecValueAs(int id) const {
        std::shared_lock lk(exec_mtx_);

        auto it = executor_status_.find(id);

        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found");

        if (!it->second.valid)
            throw std::runtime_error("Executor value invalid");

        return std::any_cast<T>(it->second.value);
    }

    GH_MODE getExecMode(int id) const {
        std::shared_lock lk(exec_mtx_);

        auto it = executor_status_.find(id);

        if (it == executor_status_.end())
            throw std::runtime_error("Executor id not found");

        return it->second.mode;
    }

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
        int id;
        std::string name;
        ExecEntry entry;
    };

    std::vector<ExecApiEntry> snapshotExecutors() const {

        std::shared_lock lk(exec_mtx_);

        std::unordered_map<int, std::string> id2name;

        for (const auto& kv : exec_name_to_id_)
            id2name[kv.second] = kv.first;

        std::vector<ExecApiEntry> out;

        for (const auto& kv : executor_status_) {

            ExecApiEntry e;

            e.id = kv.first;

            auto it = id2name.find(kv.first);

            if (it != id2name.end())
                e.name = it->second;

            e.entry = kv.second;

            out.push_back(e);
        }

        return out;
    }

    // ------------------------------------------------------------
    // Write helpers
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

    void setExec(int id, std::any value, GH_MODE mode) {

        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];

        e.value = std::move(value);
        e.mode = mode;
        e.valid = true;
        e.stampMs = nowMs();
    }

    void setExecInvalid(int id) {

        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];

        e.valid = false;
        e.stampMs = nowMs();
    }

    void setExecMode(int id, GH_MODE mode) {

        std::unique_lock lk(exec_mtx_);

        auto& e = executor_status_[id];

        e.mode = mode;
        e.stampMs = nowMs();
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