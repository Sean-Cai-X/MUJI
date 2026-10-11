// src/glue/AbstractAdapter.h
// Every module adapter (cppjieba, WordNet, GoldenDict, Kwiki, vector engine)
// inherits this. The base contract guarantees:
//   - Zero state ownership: adapter only forwards calls + converts formats
//   - Native entry point preserved: adapter does NOT replace/rewrite the
//     original binary/API; it only wraps it
//   - Uniform lifecycle: init → [query...] → shutdown
//   - Thread-safe status + error reporting

#pragma once

#include "data/SemanticTypes.h"
#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace muji {

enum class AdapterStatus : uint8_t {
    Uninitialized = 0,
    Ready         = 1,
    Error         = 2,
    Shutdown      = 3,
};

inline const char* to_cstr(AdapterStatus s) {
    switch (s) {
        case AdapterStatus::Uninitialized: return "Uninitialized";
        case AdapterStatus::Ready:         return "Ready";
        case AdapterStatus::Error:         return "Error";
        case AdapterStatus::Shutdown:      return "Shutdown";
    }
    return "?";
}

// Base class. Subclass per module.
class AbstractAdapter {
public:
    explicit AbstractAdapter(std::string name) : name_(std::move(name)) {}
    virtual ~AbstractAdapter() = default;

    // Non-copyable, non-movable (adapters hold native resource handles).
    AbstractAdapter(const AbstractAdapter&) = delete;
    AbstractAdapter& operator=(const AbstractAdapter&) = delete;

    // ── Lifecycle (override in subclass) ────────────────────────────
    // Returns true on success. On failure, set_last_error() inside.
    virtual bool init()       = 0;
    virtual bool shutdown()   = 0;

    // ── Status ──────────────────────────────────────────────────────
    AdapterStatus status() const {
        std::lock_guard<std::mutex> lk(mu_);
        return status_;
    }
    const std::string& name() const { return name_; }

    // Last error message (empty if none).
    std::string last_error() const {
        std::lock_guard<std::mutex> lk(mu_);
        return last_error_;
    }

    // ── Helpers for subclasses ──────────────────────────────────────
protected:
    void set_status(AdapterStatus s) const {
        std::lock_guard<std::mutex> lk(mu_);
        status_ = s;
    }
    void set_last_error(std::string msg) const {
        std::lock_guard<std::mutex> lk(mu_);
        last_error_ = std::move(msg);
        status_ = AdapterStatus::Error;
    }

    const std::string name_;
    mutable std::mutex mu_;
    mutable AdapterStatus status_ = AdapterStatus::Uninitialized;
    mutable std::string   last_error_;
};

} // namespace muji
