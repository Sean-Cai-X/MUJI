// src/adapters/LanAgentAdapter.h
// Glue-layer adapter for codex-lan-agent MCP service.
//
// Native entry point: codex_lan_agent binary (HTTP JSON-RPC on port 18080).
// This adapter is a pure-socket HTTP/1.1 POST client — no libcurl needed,
// no lan-agent headers linked.  Zero-install, zero-deps.
//
// Two adapters in this project talk network:
//   - GoldenDictAdapter  → dictd RFC 2229 TCP (plain ASCII, CRLF terminated)
//   - LanAgentAdapter    → codex-lan-agent MCP HTTP POST (JSON-RPC 2.0)
// Both use the same fcntl(O_NONBLOCK)+select() connect-timeout pattern.
//
// Design: lan-agent stays as independent process.  MUJI is a **consumer**
// of its MCP JSON-RPC interface, not a linker of its C++ symbols.
// Reason: lan-agent has Clang/LLVM dependency stack that's way too heavy
// to fold into MUJI's zero-install paradigm.
//
// MCP surface used here (single-gateway, as designed by lan-agent):
//   - tools/list                  → 发现可见工具 (默认只返回 lan_agent_mcp_route)
//   - tools/call  lan_agent_health → 运行时健康检查
//   - tools/call  lan_agent_mcp_route mode=overview → 能力概览
//   - tools/call  lan_agent_mcp_route mode=route   → 路由决策
//   - tools/call  lan_agent_mcp_route mode=call    → 直接执行内部工具
#pragma once

#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"

#include <string>
#include <vector>
#include <functional>

// nlohmann/json — header-only, vendored in 3rdparty/nlohmann/json.hpp
// (git repo: nlohmann/json, license MIT, 194KB single header)
#include <nlohmann/json.hpp>

namespace muji {

using json = nlohmann::json;

struct LanAgentConfig {
    std::string host = "127.0.0.1";
    int         port = 18080;
    std::string mcp_path = "/mcp";
    int         timeout_ms = 10000;  // connect + recv timeout
};

// ── Parsed MCP responses ────────────────────────────────────────────
struct McpHealth {
    bool        ok = false;
    int         exit_code = -1;
    std::string status;            // "success" / "failed"
    std::string workspace_root;
    std::string generation_endpoint;
    std::string local_chat_endpoint;
    std::string log_root;
    bool        generation_ready = false;
    bool        local_chat_ready = false;
    int         audit_field_count = 0;
};

struct McpRouteResult {
    // mode=route 时 lan-agent_mcp_route 返回:
    std::string route_target;          // 目标内部工具名
    std::string required_tool_name;
    std::string required_tool_arguments_json;
    std::string executor_decision;     // direct_mcp / delegate_to_llm / ...
    std::string next_call_json;
    bool        auto_execute_direct = false;
    bool        execution_performed = false;
    std::string executed_tool_name;
    std::string chain_state;            // needs_tool_call / complete / ...
    std::string compact_summary;
    // mode=overview 时:
    std::string visible_tool_name;
    int         visible_tool_count = 0;
    std::string mcp_route_mode;
};

class LanAgentAdapter : public AbstractAdapter {
public:
    LanAgentAdapter();
    ~LanAgentAdapter() override;

    bool init() override;
    bool shutdown() override;

    void set_config(const LanAgentConfig& c) { config_ = c; }
    const LanAgentConfig& config() const    { return config_; }

    // ── High-level API ──

    // Health check — fills context.lan_agent
    bool health(McpHealth* out = nullptr);

    // List visible tools (default = only "lan_agent_mcp_route" single gateway)
    std::vector<std::string> list_tools();

    // Route a natural-language intent through lan_agent_mcp_route mode=route
    McpRouteResult route_intent(const std::string& request_text,
                                const std::string& primary_intent = "");

    // Get MCP capability overview (mode=overview)
    McpRouteResult overview();

    // Direct call through mode=call
    json route_call(const std::string& target_tool_name,
                    const json& arguments = json::object());

    // Raw: POST any JSON-RPC body and parse response
    json raw_call(const json& request_body);

    // Convenience: auto-run through full chain (route → call → parse result)
    // Returns tool call result, or null if route decision says delegate
    json auto_exec(const std::string& request_text);

private:
    LanAgentConfig config_;
    int next_id_ = 1;  // JSON-RPC request id counter

    // ── Low-level HTTP POST ──
    // Opens TCP connection, sends HTTP/1.1 POST with JSON body, parses
    // response into (headers, body).  Returns true on success.
    struct HttpResponse {
        int         status_code = 0;
        std::string status_line;
        std::string content_type;
        std::string body;
    };
    bool http_post_json(const std::string& path,
                        const std::string& json_body,
                        HttpResponse* out);

    // Socket helper (same pattern as GoldenDictAdapter)
    int connect_tcp() const;
    std::string read_http_body(int fd, size_t content_length) const;
};

} // namespace muji
