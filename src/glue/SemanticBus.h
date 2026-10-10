// src/glue/SemanticBus.h
// Central registry + routing for all adapters.
//
// The bus is intentionally minimal: it does NOT parse text, run queries,
// or hold semantic state. It only:
//   - Owns adapter instances
//   - Routes "give me adapter X by name" calls
//   - Collects global log lines
// This preserves the "native modules keep their own entry points" rule.

#pragma once

#include "AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <functional>
#include <chrono>
#include <sstream>

namespace muji {

struct LogLine {
    std::string timestamp;   // ISO-ish
    std::string adapter;     // adapter name or "bus"
    std::string message;
};

class SemanticBus {
public:
    static SemanticBus& instance() {
        static SemanticBus inst;
        return inst;
    }

    // ── Adapter registry ────────────────────────────────────────────
    // Owns the adapter pointer. Call once per adapter at startup.
    void register_adapter(std::shared_ptr<AbstractAdapter> adapter) {
        std::lock_guard<std::mutex> lk(mu_);
        adapters_[adapter->name()] = std::move(adapter);
    }

    template<typename T>
    std::shared_ptr<T> get(const std::string& name) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = adapters_.find(name);
        if (it == adapters_.end()) return nullptr;
        return std::dynamic_pointer_cast<T>(it->second);
    }

    std::vector<std::string> list_adapters() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<std::string> out;
        out.reserve(adapters_.size());
        for (auto& [k, _] : adapters_) out.push_back(k);
        return out;
    }

    // ── Lifecycle helpers ───────────────────────────────────────────
    // Init all registered adapters; return number that failed.
    size_t init_all() {
        size_t failed = 0;
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [name, ad] : adapters_) {
            bus_log("bus", "init adapter: " + name);
            if (!ad->init()) {
                bus_log("bus", "FAILED init: " + name + " — " + ad->last_error());
                ++failed;
            } else {
                bus_log("bus", "ready: " + name);
            }
        }
        return failed;
    }

    void shutdown_all() {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [name, ad] : adapters_) {
            bus_log("bus", "shutdown: " + name);
            ad->shutdown();
        }
    }

    // ── Log ─────────────────────────────────────────────────────────
    void log(const std::string& adapter, const std::string& msg) {
        bus_log(adapter, msg);
    }

    std::vector<LogLine> recent_logs(size_t max = 256) const {
        std::lock_guard<std::mutex> lk(log_mu_);
        if (logs_.size() <= max) return logs_;
        return std::vector<LogLine>(logs_.end() - max, logs_.end());
    }

    // ── Cross-layer context ──────────────────────────────────────────
    // The bus holds ONE active SemanticContext (for convenience).
    // Downstream adapters read/write it directly. Call clear_context()
    // before a new pipeline run.
    SemanticContext& active_context() { return ctx_; }
    void clear_context() { ctx_ = SemanticContext{}; }

private:
    SemanticBus() = default;

    void bus_log(const std::string& adapter, const std::string& msg) {
        auto now = std::chrono::system_clock::now();
        auto sec = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        std::ostringstream ts;
        ts << sec;
        {
            std::lock_guard<std::mutex> lk(log_mu_);
            logs_.push_back({ts.str(), adapter, msg});
            if (logs_.size() > 4096) logs_.erase(logs_.begin());
        }
    }

    mutable std::mutex mu_;
    std::unordered_map<std::string, std::shared_ptr<AbstractAdapter>> adapters_;

    mutable std::mutex log_mu_;
    std::vector<LogLine> logs_;

    SemanticContext ctx_;
};

// Convenience macro-ish: register an adapter by constructing a shared_ptr
template<typename T, typename... Args>
std::shared_ptr<T> make_and_register(Args&&... args) {
    auto p = std::make_shared<T>(std::forward<Args>(args)...);
    SemanticBus::instance().register_adapter(p);
    return p;
}

} // namespace muji
