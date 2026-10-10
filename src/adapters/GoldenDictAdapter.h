// src/adapters/GoldenDictAdapter.h
// Glue-layer adapter for GoldenDict (layer 2 — dictionary aggregation).
//
// Native entry point: dictd server (system apt package, listens on TCP 2628).
// This adapter talks RFC 2229 DICT protocol directly via sockets — no library
// deps, no dictd client dependency, and completely decoupled from GoldenDict
// the Qt GUI application.
//
// Design choice rationale (AGENTS.md says "GoldenDict -> Qt dic gui"):
//   GoldenDict the GUI app is a Qt desktop tool — you can't "wrap" it as a
//   library.  Its job (aggregating multiple dict sources) is accomplished
//   at the protocol level: dictd already loads gcide + wn + any StarDict
//   files at /usr/share/dictd/  and exposes them via RFC 2229.
//   So "GoldenDict" in MUJI terms = dictd protocol client.  The real Qt
//   GoldenDict GUI stays as an independent native entry point for manual
//   deep-dive dictionary browsing.

#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>

namespace muji {

// DictEntry is now defined in data/SemanticTypes.h (shared by all adapters).

struct GoldenDictConfig {
    std::string host = "127.0.0.1";
    int         port = 2628;
    std::string default_db = "*";  // "*" = all DBs, "!" = first-match
    bool        auto_start_dictd = true;  // if dictd not running, try `sudo dictd &`
};

class GoldenDictAdapter : public AbstractAdapter {
public:
    GoldenDictAdapter();
    ~GoldenDictAdapter() override;

    bool init() override;
    bool shutdown() override;

    void set_config(const GoldenDictConfig& c) { config_ = c; }
    const GoldenDictConfig& config() const    { return config_; }

    // ── Query API ──
    // DEFINE word [in database]
    std::vector<DictEntry> define(const std::string& word,
                                  const std::string& database = "") const;

    // MATCH word [in database] using strategy ("prefix"/"substring"/"." any)
    std::vector<std::string> match(const std::string& word,
                                   const std::string& database = "",
                                   const std::string& strategy = ".") const;

    // LIST databases (SHOW DB)
    std::vector<std::pair<std::string, std::string>> list_databases() const;

    // Raw RFC 2229 query — returns full response
    std::string raw_query(const std::string& command) const;

private:
    GoldenDictConfig config_;

    // Low-level TCP: returns socket fd, or -1 on error
    int connect_dictd() const;
    void close_socket(int fd) const;
    std::string read_all(int fd) const;
};

} // namespace muji
