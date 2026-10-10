// src/native/dict_cli.cpp
// === NATIVE ENTRY POINT for GoldenDict (layer 2) ===
//
// Standalone CLI wrapping RFC 2229 DICT protocol.  Exactly mirrors the
// GoldenDictAdapter API.  Bypasses MUJI glue entirely.
//
// Compile:
//   g++ -std=c++17 src/native/dict_cli.cpp -o build/bin/dict_cli
//
// Examples:
//   dict_cli define memory                        # all DBs
//   dict_cli define memory -d gcide               # gcide only
//   dict_cli match memory -s prefix               # prefix match
//   dict_cli list                                 # list databases

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void usage(const char* p) {
    std::fprintf(stderr,
        "Usage: %s <cmd> [args]\n"
        "  define <word>        look up word\n"
        "  match  <word>        match word (strategy: -s prefix|substring|. )\n"
        "  list                 list available databases\n"
        "  raw    <rfc2229 cmd> send raw command\n"
        "  -d <db>   database id (gcide|wn|* default)\n"
        "  -s <strat> strategy for match (default .)\n"
        "  -H host   dictd host [127.0.0.1]\n"
        "  -P port   dictd port [2628]\n", p);
}

#include <fcntl.h>
#include <sys/select.h>

static int dictd_connect(const char* host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    // Nonblocking connect — SO_SNDTIMEO doesn't apply to connect() on Linux
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr(host);

    int rc = connect(fd, (sockaddr*)&a, sizeof(a));
    if (rc == 0) {
        fcntl(fd, F_SETFL, flags);
    } else if (errno == EINPROGRESS) {
        fd_set wfds; FD_ZERO(&wfds); FD_SET(fd, &wfds);
        struct timeval tv{3, 0};
        rc = select(fd + 1, nullptr, &wfds, nullptr, &tv);
        if (rc <= 0) { close(fd); return -1; }
        fcntl(fd, F_SETFL, flags);
    } else {
        close(fd); return -1;
    }

    struct timeval rcv_tv{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv));

    // Read server banner
    char b[256];
    recv(fd, b, sizeof(b) - 1, 0);
    return fd;
}

static std::string dictd_query(int fd, const std::string& cmd) {
    std::string req = cmd + "\r\n";
    send(fd, req.data(), req.size(), 0);
    std::string out;
    char buf[4096];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;  // EOF or timeout
        out.append(buf, n);
        // Check if last complete line is a status code (2xx/3xx/4xx/5xx)
        auto idx = out.rfind("\r\n");
        if (idx != std::string::npos) {
            std::string last = out.substr(idx + 2);
            if (last.empty()) {
                // out ends with \r\n — look back for previous line
                auto idx2 = out.rfind("\r\n", idx - 1);
                if (idx2 != std::string::npos)
                    last = out.substr(idx2 + 2, idx - idx2 - 2);
            }
            if (!last.empty() && last.size() >= 3) {
                char c = last[0];
                if (c == '2' || c == '3' || c == '4' || c == '5') break;
            }
        }
    }
    return out;
}

static void print_raw(const std::string& raw) {
    // Print lines between "From ..." and next "From" or "."
    std::string line;
    size_t i = 0;
    while (i < raw.size()) {
        auto j = raw.find("\r\n", i);
        if (j == std::string::npos) j = raw.size();
        line = raw.substr(i, j - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::printf("%s\n", line.c_str());
        i = j + 2;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    const char* host = "127.0.0.1";
    int port = 2628;
    const char* db = "*";
    const char* strat = ".";
    const char* word = nullptr;
    std::string command_str;

    int i = 1;
    for (; i < argc; ++i) {
        const char* a = argv[i];
        if (!strcmp(a, "-H") && i+1 < argc) host = argv[++i];
        else if (!strcmp(a, "-P") && i+1 < argc) port = std::atoi(argv[++i]);
        else if (!strcmp(a, "-d") && i+1 < argc) db = argv[++i];
        else if (!strcmp(a, "-s") && i+1 < argc) strat = argv[++i];
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "define") || !strcmp(a, "match") || !strcmp(a, "list") || !strcmp(a, "raw")) {
            command_str = a;
        } else if (command_str.empty()) { usage(argv[0]); return 1; }
        else if (!word) word = a;
    }

    int fd = dictd_connect(host, port);
    if (fd < 0) {
        std::fprintf(stderr, "cannot connect to dictd at %s:%d — is 'dictd' running?\n", host, port);
        return 2;
    }

    if (command_str == "list") {
        std::string r = dictd_query(fd, "SHOW DB");
        print_raw(r);
    } else if (command_str == "define") {
        if (!word) { usage(argv[0]); return 1; }
        std::string r = dictd_query(fd, std::string("DEFINE ") + db + " " + word);
        print_raw(r);
    } else if (command_str == "match") {
        if (!word) { usage(argv[0]); return 1; }
        std::string r = dictd_query(fd, std::string("MATCH ") + db + " " + strat + " " + word);
        print_raw(r);
    } else if (command_str == "raw") {
        // Reconstruct raw command from remaining args
        std::string cmd;
        for (int k = 1; k < argc; ++k) {
            if (k > 1) cmd += " ";
            cmd += argv[k];
        }
        // strip leading "raw "
        auto pos = cmd.find("raw ");
        if (pos != std::string::npos) cmd = cmd.substr(pos + 4);
        std::string r = dictd_query(fd, cmd);
        print_raw(r);
    } else {
        usage(argv[0]);
        return 1;
    }

    send(fd, "QUIT\r\n", 6, 0);
    close(fd);
    return 0;
}
