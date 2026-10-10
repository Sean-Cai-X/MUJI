// src/adapters/LanAgentAdapter.cpp
// Pure-socket HTTP/1.1 POST client for codex-lan-agent MCP (JSON-RPC 2.0).
// Zero deps — no libcurl, no Boost.Beast, no lan-agent headers.
//
// Wire format lan-agent MCP speaks:
//   POST /mcp HTTP/1.1
//   Host: 127.0.0.1:18080
//   Content-Type: application/json
//   Content-Length: N
//
//   {"jsonrpc":"2.0","id":1,"method":"tools/call",
//    "params":{"name":"lan_agent_health","arguments":{}}}
//
//   HTTP/1.1 200 OK
//   Content-Type: application/json
//   Content-Length: M
//
//   {"jsonrpc":"2.0","id":1,"result":{"content":[...],"structuredContent":{...}}}

#include "LanAgentAdapter.h"

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

LanAgentAdapter::LanAgentAdapter() : AbstractAdapter("LanAgent") {}
LanAgentAdapter::~LanAgentAdapter() { shutdown(); }

// ── Socket connect (same O_NONBLOCK + select pattern as GoldenDictAdapter) ──
int LanAgentAdapter::connect_tcp() const {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int timeout_sec = std::max(1, config_.timeout_ms / 1000);
    struct timeval tv{timeout_sec, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(config_.port);
    addr.sin_addr.s_addr = inet_addr(config_.host.c_str());

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, (sockaddr*)&addr, sizeof(addr));
    if (rc != 0 && errno == EINPROGRESS) {
        fd_set writefds;
        FD_ZERO(&writefds);
        FD_SET(fd, &writefds);
        struct timeval ctv{timeout_sec, 0};
        rc = select(fd + 1, nullptr, &writefds, nullptr, &ctv);
        if (rc <= 0) { close(fd); return -1; }
    } else if (rc != 0) {
        close(fd); return -1;
    }
    fcntl(fd, F_SETFL, flags);  // restore blocking
    return fd;
}

std::string LanAgentAdapter::read_http_body(int fd, size_t content_length) const {
    std::string out;
    out.reserve(content_length > 0 ? content_length : 4096);
    char buf[8192];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, n);
        if (content_length > 0 && out.size() >= content_length) break;
    }
    return out;
}

bool LanAgentAdapter::http_post_json(const std::string& path,
                                    const std::string& json_body,
                                    HttpResponse* out) {
    if (!out) return false;
    out->status_code = 0;

    int fd = connect_tcp();
    if (fd < 0) {
        set_last_error("cannot connect to lan-agent MCP at " + config_.host + ":" +
                       std::to_string(config_.port));
        return false;
    }

    // Build HTTP/1.1 POST request
    char header_buf[1024];
    std::snprintf(header_buf, sizeof(header_buf),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        path.c_str(), config_.host.c_str(), config_.port, json_body.size());

    std::string request = header_buf;
    request += json_body;

    ssize_t sent = send(fd, request.data(), request.size(), 0);
    if (sent <= 0) { close(fd); set_last_error("send failed"); return false; }

    // Read response headers until \r\n\r\n
    std::string header_raw;
    char hbuf[4096];
    size_t content_length = 0;
    bool header_end = false;

    while (!header_end) {
        ssize_t n = recv(fd, hbuf, sizeof(hbuf), 0);
        if (n <= 0) break;
        header_raw.append(hbuf, n);
        auto end_marker = header_raw.find("\r\n\r\n");
        if (end_marker != std::string::npos) {
            // Extract Content-Length
            std::string headers_part = header_raw.substr(0, end_marker);
            auto cl_pos = headers_part.find("Content-Length:");
            if (cl_pos == std::string::npos) cl_pos = headers_part.find("content-length:");
            if (cl_pos != std::string::npos) {
                auto line_start = headers_part.find(':', cl_pos) + 1;
                auto line_end   = headers_part.find("\r\n", line_start);
                if (line_end == std::string::npos) line_end = headers_part.size();
                content_length = std::stoul(headers_part.substr(line_start, line_end - line_start));
            }
            // Also check chunked? lan-agent should return Content-Length for JSON
            header_end = true;

            // Parse status line
            auto first_nl = headers_part.find("\r\n");
            if (first_nl == std::string::npos) first_nl = headers_part.size();
            out->status_line = headers_part.substr(0, first_nl);
            // "HTTP/1.1 200 OK"
            auto sp1 = out->status_line.find(' ');
            if (sp1 != std::string::npos) {
                auto sp2 = out->status_line.find(' ', sp1 + 1);
                std::string code = (sp2 == std::string::npos)
                    ? out->status_line.substr(sp1 + 1)
                    : out->status_line.substr(sp1 + 1, sp2 - sp1 - 1);
                try { out->status_code = std::stoi(code); } catch (...) { out->status_code = 0; }
            }

            // Read body after header
            size_t already_read = header_raw.size() - (end_marker + 4);
            if (already_read > 0) {
                out->body = header_raw.substr(end_marker + 4);
            }
            if (out->body.size() < content_length) {
                size_t need = content_length - out->body.size();
                out->body += read_http_body(fd, need);
            }
        }
    }

    // Fallback: read rest of socket (handles chunked or missing Content-Length)
    if (!header_end || out->body.empty()) {
        out->body += read_http_body(fd, 0);
    }

    close(fd);
    return out->status_code >= 200 && out->status_code < 300 && !out->body.empty();
}

// ── init / shutdown ──
bool LanAgentAdapter::init() {
    // Quick health probe
    McpHealth h;
    if (!health(&h) || !h.ok) {
        set_last_error("lan-agent MCP not reachable at " + config_.host + ":" +
                       std::to_string(config_.port) + " — start lan-agent serve first");
        return false;
    }
    set_status(AdapterStatus::Ready);
    return true;
}

bool LanAgentAdapter::shutdown() {
    set_status(AdapterStatus::Shutdown);
    return true;
}

// ── raw_call ──
json LanAgentAdapter::raw_call(const json& request_body) {
    // Allow in any state except Error/Shutdown — init() needs to call health()
    // before set_status(Ready); GUI panels also call after shutdown we don't want.
    auto st = status();
    if (st == AdapterStatus::Error || st == AdapterStatus::Shutdown) return nullptr;

    // Auto-assign id if missing
    json body = request_body;
    if (!body.contains("id")) {
        body["id"] = next_id_++;
    }

    std::string path = config_.mcp_path;
    if (path.empty()) path = "/mcp";

    HttpResponse resp;
    if (!http_post_json(path, body.dump(), &resp)) return nullptr;

    try {
        return json::parse(resp.body);
    } catch (...) {
        return nullptr;
    }
}

// ── health ──
bool LanAgentAdapter::health(McpHealth* out) {
    if (!out) { out = new McpHealth(); }

    json req = {
        {"jsonrpc", "2.0"},
        {"method", "tools/call"},
        {"params", {
            {"name", "lan_agent_health"},
            {"arguments", json::object()}
        }}
    };

    json resp = raw_call(req);
    if (resp.is_null() || !resp.contains("result")) return false;

    // lan-agent health result has structuredContent with ok/exit_code/...
    const auto& sc = resp.value("/result/structuredContent"_json_pointer, json::object());
    out->ok            = sc.value("ok", false);
    out->exit_code     = sc.value("exit_code", -1);
    out->status        = sc.value("status", "");
    out->workspace_root = sc.value("workspace_root", "");
    out->generation_endpoint = sc.value("generation_endpoint", "");
    out->log_root      = sc.value("log_root", "");
    out->generation_ready = (sc.value("generation_ready", "") == "true");
    out->local_chat_ready = (sc.value("local_chat_ready", "") == "true");
    out->audit_field_count = std::stoi(sc.value("audit_field_count", "0"));

    return out->ok;
}

// ── list_tools ──
std::vector<std::string> LanAgentAdapter::list_tools() {
    std::vector<std::string> names;
    json req = {
        {"jsonrpc", "2.0"},
        {"method", "tools/list"},
        {"params", json::object()}
    };
    json resp = raw_call(req);
    if (resp.is_null() || !resp.contains("result") || !resp["result"].contains("tools")) return names;
    for (const auto& t : resp["result"]["tools"]) {
        if (t.contains("name")) names.push_back(t["name"].get<std::string>());
    }
    return names;
}

// ── route_intent ──
McpRouteResult LanAgentAdapter::route_intent(const std::string& request_text,
                                             const std::string& primary_intent) {
    McpRouteResult r;

    json args = {{"mode", "route"}};
    if (!request_text.empty()) args["request_text"] = request_text;
    if (!primary_intent.empty()) args["primary_intent"] = primary_intent;

    json req = {
        {"jsonrpc", "2.0"},
        {"method", "tools/call"},
        {"params", {
            {"name", "lan_agent_mcp_route"},
            {"arguments", args}
        }}
    };
    json resp = raw_call(req);
    if (resp.is_null() || !resp.contains("result")) return r;

    const auto& sc = resp.value("/result/structuredContent"_json_pointer, json::object());
    r.mcp_route_mode      = sc.value("mcp_route_mode", "");
    r.executor_decision   = sc.value("executor_decision", "");
    r.route_target        = sc.value("route_target", "");
    r.required_tool_name  = sc.value("required_tool_name", "");
    r.required_tool_arguments_json = sc.value("required_tool_arguments_json", "");
    r.next_call_json      = sc.value("next_call_json", "");
    r.auto_execute_direct = sc.value("auto_execute_direct", false);
    r.execution_performed = sc.value("execution_performed", false);
    r.executed_tool_name  = sc.value("executed_tool_name", "");
    r.chain_state         = sc.value("chain_state", "");

    return r;
}

// ── overview ──
McpRouteResult LanAgentAdapter::overview() {
    McpRouteResult r;
    json req = {
        {"jsonrpc", "2.0"},
        {"method", "tools/call"},
        {"params", {
            {"name", "lan_agent_mcp_route"},
            {"arguments", {{"mode", "overview"}}}
        }}
    };
    json resp = raw_call(req);
    if (resp.is_null() || !resp.contains("result")) return r;

    const auto& sc = resp.value("/result/structuredContent"_json_pointer, json::object());
    r.mcp_route_mode    = sc.value("mcp_route_mode", "");
    r.visible_tool_name = sc.value("visible_tool_name", "");
    r.visible_tool_count = std::stoi(sc.value("visible_tool_count", "0"));
    r.chain_state       = sc.value("chain_state", "");

    return r;
}

// ── route_call ──
json LanAgentAdapter::route_call(const std::string& target_tool_name,
                                 const json& arguments) {
    json req = {
        {"jsonrpc", "2.0"},
        {"method", "tools/call"},
        {"params", {
            {"name", "lan_agent_mcp_route"},
            {"arguments", {
                {"mode", "call"},
                {"target_tool_name", target_tool_name},
                {"arguments", arguments}
            }}
        }}
    };
    return raw_call(req);
}

// ── auto_exec ──
json LanAgentAdapter::auto_exec(const std::string& request_text) {
    McpRouteResult r = route_intent(request_text);
    // mode=direct_mcp: lan-agent may have auto-executed internally
    if (r.execution_performed) {
        // We got result inside route response — return it
        // (lan-agent auto_execute_direct=true 时 route 就执行了)
        return nullptr; // caller should inspect r directly
    }
    // Otherwise do mode=call with the decided tool
    if (!r.required_tool_name.empty()) {
        json args = json::parse(r.required_tool_arguments_json.empty() ? "{}"
                             : r.required_tool_arguments_json);
        return route_call(r.required_tool_name, args);
    }
    return nullptr;
}

} // namespace muji
