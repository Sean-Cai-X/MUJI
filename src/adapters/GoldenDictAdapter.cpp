// src/adapters/GoldenDictAdapter.cpp
// RFC 2229 DICT protocol client (tiny socket implementation).
// Wire format: ASCII text, CRLF terminated.
//   > DEFINE gcide memory\r\n
//   < 150 1 definition found\r\n
//   < ...definition text...\r\n
//   < .\r\n
//   < 250 ok\r\n

#include "GoldenDictAdapter.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <thread>
#include <chrono>

namespace muji {

GoldenDictAdapter::GoldenDictAdapter() : AbstractAdapter("GoldenDict") {}
GoldenDictAdapter::~GoldenDictAdapter() { shutdown(); }

int GoldenDictAdapter::connect_dictd() const {
    FILE* dbg = std::fopen("/tmp/muji_dbg_connect.txt", "a");
    auto log = [&](const char* m) { if (dbg) { std::fprintf(dbg, "[connect] %s\n", m); std::fflush(dbg); } };
    log("start");

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { log("socket failed"); return -1; }
    log("socket ok");

    // 5s timeout on recv AND on connect (via non-blocking trick)
    struct timeval tv{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(config_.port);
    addr.sin_addr.s_addr = inet_addr(config_.host.c_str());

    // Set non-blocking to implement our own connect timeout
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, (sockaddr*)&addr, sizeof(addr));
    if (rc != 0) {
        if (errno != EINPROGRESS) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "connect failed rc=%d errno=%d(%s)", rc, errno, strerror(errno));
            log(buf);
            close(fd);
            if (dbg) std::fclose(dbg);
            return -1;
        }
        // Wait up to 5s for connect to complete
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(fd, &writefds);
        struct timeval ctv{5, 0};
        rc = select(fd + 1, nullptr, &writefds, nullptr, &ctv);
        if (rc <= 0) {
            log("connect timeout");
            close(fd);
            if (dbg) std::fclose(dbg);
            return -1;
        }
    }
    log("connect returned");

    // Restore blocking
    fcntl(fd, F_SETFL, flags);
    log("blocking restored");

    char buf[512];
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    char rlog[256];
    std::snprintf(rlog, sizeof(rlog), "recv n=%ld", (long)n);
    log(rlog);
    if (n <= 0) { close(fd); if (dbg) std::fclose(dbg); return -1; }
    buf[n] = '\0';
    if (std::string(buf).find("220 ") != 0) { close(fd); if (dbg) std::fclose(dbg); return -1; }
    if (dbg) std::fclose(dbg);
    return fd;
}

void GoldenDictAdapter::close_socket(int fd) const {
    if (fd >= 0) {
        const char* quit = "QUIT\r\n";
        send(fd, quit, strlen(quit), 0);
        close(fd);
    }
}

std::string GoldenDictAdapter::read_all(int fd) const {
    std::string out;
    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, n);
        // 250 = ok, 552 = no match — both end a command response
        // We read until socket closes or until we see a terminating status line
        // For simplicity: keep reading until 1s quiet period OR we see "250" / "552" at line start
    }
    return out;
}

bool GoldenDictAdapter::init() {
    // Test connection
    int fd = connect_dictd();
    if (fd < 0) {
        if (config_.auto_start_dictd) {
            // Try starting dictd
            int rc = std::system("which dictd 2>/dev/null && (dictd 2>/dev/null || sudo dictd 2>/dev/null) &");
            (void)rc;
            // Wait up to 3s
            for (int i = 0; i < 15 && fd < 0; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                fd = connect_dictd();
            }
        }
        if (fd < 0) {
            set_last_error("cannot connect to dictd at " + config_.host + ":" +
                           std::to_string(config_.port) +
                           " — apt install dictd dict-gcide dict-wn");
            return false;
        }
    }
    close_socket(fd);
    set_status(AdapterStatus::Ready);
    return true;
}

bool GoldenDictAdapter::shutdown() {
    set_status(AdapterStatus::Shutdown);
    return true;
}

std::string GoldenDictAdapter::raw_query(const std::string& command) const {
    if (status() != AdapterStatus::Ready) return {};
    int fd = connect_dictd();
    if (fd < 0) return {};

    std::string req = command;
    if (req.size() < 2 || req[req.size()-2] != '\r' || req[req.size()-1] != '\n') {
        req += "\r\n";
    }
    send(fd, req.data(), req.size(), 0);

    // Read in chunks until we see terminal code 250 / 552 / 4xx / 5xx
    std::string out;
    char buf[4096];
    auto is_terminal = [&](const std::string& line) {
        if (line.size() < 4) return false;
        char c0 = line[0];
        if (c0 == '2' || c0 == '3' || c0 == '4' || c0 == '5') return true;
        return false;
    };
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, n);
        // Check if last complete line is a terminal status
        auto last_nl = out.rfind("\r\n");
        if (last_nl != std::string::npos) {
            std::string last_line = out.substr(last_nl + 2);
            if (!last_line.empty() && last_line != "\r\n") {
                // Get the line before last
                auto prev_nl = out.rfind("\r\n", last_nl - 1);
                std::string check = (prev_nl == std::string::npos) ? last_line
                                   : out.substr(prev_nl + 2, last_nl - prev_nl - 2);
                if (is_terminal(check)) break;
            }
        }
    }

    close_socket(fd);
    return out;
}

std::vector<DictEntry> GoldenDictAdapter::define(const std::string& word,
                                                 const std::string& database) const {
    std::vector<DictEntry> out;
    std::string db = database.empty() ? config_.default_db : database;

    std::string cmd = "DEFINE " + db + " " + word;
    std::string raw = raw_query(cmd);
    if (raw.empty()) return out;

    // Split response by "150 N definitions found" markers
    // And extract database tag + body
    std::istringstream iss(raw);
    std::string line;
    std::string current_db, current_dbname;
    std::string body;
    bool in_body = false;
    while (std::getline(iss, line)) {
        // strip trailing \r
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // "150 N definitions found"
        if (line.find("150 ") == 0) {
            // extract DB name from next "From XXX [db]:" block
            continue;
        }
        // "From WordNet (r) 3.0 (2006) [wn]:"
        if (line.find("From ") == 0) {
            // Save previous entry if any
            if (!current_db.empty() && !body.empty()) {
                DictEntry e;
                e.database = current_db;
                e.db_name  = current_dbname;
                e.word = word;
                e.definition = body;
                e.source = SourceTag::GoldenDict;
                out.push_back(std::move(e));
            }
            // Parse "From XXX [db]:"
            auto bracket = line.find('[');
            auto close = line.find(']');
            if (bracket != std::string::npos && close != std::string::npos) {
                current_db = line.substr(bracket + 1, close - bracket - 1);
                current_dbname = line.substr(5, bracket - 6); // "From " = 5, minus ": "
            }
            body.clear();
            in_body = true;
            continue;
        }
        if (line == ".") { in_body = false; continue; }
        if (in_body) {
            if (!body.empty()) body += "\n";
            body += line;
        }
    }
    // Last entry
    if (!current_db.empty() && !body.empty()) {
        DictEntry e;
        e.database = current_db;
        e.db_name  = current_dbname;
        e.word = word;
        e.definition = body;
        e.source = SourceTag::GoldenDict;
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<std::string> GoldenDictAdapter::match(const std::string& word,
                                                  const std::string& database,
                                                  const std::string& strategy) const {
    std::vector<std::string> out;
    std::string db = database.empty() ? config_.default_db : database;
    std::string cmd = "MATCH " + db + " " + strategy + " " + word;
    std::string raw = raw_query(cmd);
    if (raw.empty()) return out;
    std::istringstream iss(raw);
    std::string line;
    bool in_list = false;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find("251 ") == 0) in_list = true;
        if (line == ".") in_list = false;
        if (in_list && !line.empty() && line[0] != '2') {
            // format: db\tword
            auto tab = line.find('\t');
            if (tab != std::string::npos) out.push_back(line.substr(tab + 1));
            else out.push_back(line);
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> GoldenDictAdapter::list_databases() const {
    std::vector<std::pair<std::string, std::string>> out;
    std::string raw = raw_query("SHOW DB");
    if (raw.empty()) return out;
    std::istringstream iss(raw);
    std::string line;
    bool in_list = false;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find("250 ") == 0) continue;
        if (line.find("554 ") == 0) { in_list = true; continue; }
        if (line == ".") break;
        if (in_list) {
            auto tab = line.find('\t');
            if (tab != std::string::npos) {
                std::string id = line.substr(0, tab);
                std::string name = line.substr(tab + 1);
                out.push_back({id, name});
            }
        }
    }
    return out;
}

} // namespace muji
