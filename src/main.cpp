// src/main.cpp — MUJI semantic mother-machine glue layer workspace
// Stage S8: 3 modules wired + auto-test on first frame + cross-layer pipeline
//
// All 4 panels auto-populate on first render — no button click needed.
// Pipeline panel shows Jieba→WordNet→GoldenDict chain immediately.

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

// ── 3D Scene Layer (Interface-only + CPU 3D→2D projection) ──
#include "3d/SceneTypes.h"
#include "3d/SoftwareRenderer3D.h"
#include "3d/SemanticSceneBuilder.h"

#include "adapters/JiebaAdapter.h"
#ifdef MUJI_HAS_MECAB
#include "adapters/MeCabAdapter.h"
#endif
#ifdef MUJI_HAS_WORDNET
#include "adapters/WordNetAdapter.h"
#endif
#ifdef MUJI_HAS_UDPIPE
#include "adapters/UDPipeAdapter.h"
#endif
#include "adapters/GoldenDictAdapter.h"
#include "adapters/StarDictAdapter.h"
#ifdef MUJI_HAS_KIWIX
#include "adapters/KiwixAdapter.h"
#endif
#include "adapters/VectorAdapter.h"
#ifdef MUJI_HAS_LANAGENT
#include "adapters/LanAgentAdapter.h"
#endif
#include "glue/SemanticBus.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>

// ── Forward declarations ─────────────────────────────────────────────
static void show_jieba_panel();
#ifdef MUJI_HAS_MECAB
static void show_mecab_panel();
#endif
#ifdef MUJI_HAS_WORDNET
static void show_wordnet_panel();
#endif
#ifdef MUJI_HAS_UDPIPE
static void show_german_panel();
#endif
static void show_goldendict_panel();
static void show_stardict_panel();
#ifdef MUJI_HAS_KIWIX
static void show_kiwix_panel();
#endif
static void show_vector_panel();
static void show_pipeline_panel();
#ifdef MUJI_HAS_LANAGENT
static void show_lanagent_panel();
#endif
static void show_3d_scene_panel();  // 3D 语义图谱

// ── Module lifecycle ─────────────────────────────────────────────────
static bool init_modules() {
    // Layer 0: cppjieba
    auto jieba = muji::make_and_register<muji::JiebaAdapter>();
    jieba->set_config(muji::JiebaConfig{});
    if (!jieba->init()) {
        std::fprintf(stderr, "JiebaAdapter init FAILED: %s\n", jieba->last_error().c_str());
        return false;
    }
    { FILE* t = std::fopen("/tmp/muji_trigger_0.txt", "w"); if (t) { std::fprintf(t, "0-jieba-OK\n"); std::fclose(t); } }

#ifdef MUJI_HAS_MECAB
    // Layer 0b: MeCab (Japanese morphological analyzer)
    auto mecab = muji::make_and_register<muji::MeCabAdapter>();
    muji::MeCabConfig mecab_cfg;
    // 优先 toolchain-rootfs 下的 ipadic-utf8 (bootstrap 解压)
    // 如果词典目录为空, MeCabAdapter::init() 会失败但不致命
    mecab_cfg.dicdir = "/H/aisrc/MUJI/toolchain-rootfs/var/lib/mecab/dic/ipadic-utf8";
    if (mecab->init()) {
        std::fprintf(stderr, "MeCabAdapter ready (dicdir=%s)\n", mecab_cfg.dicdir.c_str());
        { FILE* t = std::fopen("/tmp/muji_trigger_0b.txt", "w"); if (t) { std::fprintf(t, "0b-mecab-OK\n"); std::fclose(t); } }
    } else {
        std::fprintf(stderr, "MeCabAdapter init (continuing — dicdir may be empty): %s\n",
                     mecab->last_error().c_str());
    }
#endif

#ifdef MUJI_HAS_UDPIPE
    // Layer 0c: UDPipe (German + multi-language UDP pipeline)
    // mode: auto → 先测 REST (LINDAT), 通就 REST; 否则 fallback CLI
    // env: MUJI_UDPIPE_MODE (rest|cli|auto), MUJI_UDPIPE_MODEL, MUJI_UDPIPE_MODEL_PATH, MUJI_UDPIPE_BIN
    auto udp = muji::make_and_register<muji::UDPipeAdapter>();
    muji::UDPipeConfig udp_cfg;
    const char* env_mode  = std::getenv("MUJI_UDPIPE_MODE");
    const char* env_model = std::getenv("MUJI_UDPIPE_MODEL");
    const char* env_mpath = std::getenv("MUJI_UDPIPE_MODEL_PATH");
    const char* env_bin   = std::getenv("MUJI_UDPIPE_BIN");
    udp_cfg.mode       = env_mode  ? env_mode  : "auto";
    udp_cfg.model_name = env_model ? env_model : "german-gsd";
    udp_cfg.model_path = env_mpath ? env_mpath : "/H/usr/aisrc/models/de/german-gsd.udpipe";
    udp_cfg.udpipe_bin = env_bin   ? env_bin   : "/H/usr/aisrc/udpipe/udpipe";
    if (udp->init()) {
        std::fprintf(stderr, "UDPipeAdapter ready (mode=%s model=%s)\n",
                     udp_cfg.mode.c_str(), udp_cfg.model_name.c_str());
        { FILE* t = std::fopen("/tmp/muji_trigger_0c.txt", "w"); if (t) { std::fprintf(t, "0c-udpipe-OK\n"); std::fclose(t); } }
    } else {
        std::fprintf(stderr, "UDPipeAdapter init (continuing): %s\n", udp->last_error().c_str());
    }
#endif

#ifdef MUJI_HAS_WORDNET
    auto wn = muji::make_and_register<muji::WordNetAdapter>();
    muji::WordNetConfig wn_cfg;
    // WordNet 数据路径优先级: myfs > toolchain-rootfs > 系统默认
    if (auto p = std::getenv("MUJI_WN_DICT")) wn_cfg.dict_path = p;
    else if (std::filesystem::exists("/H/aisrc/ISO/iso_build/myfs/usr/share/wordnet"))
        wn_cfg.dict_path = "/H/aisrc/ISO/iso_build/myfs/usr/share/wordnet";
    else if (std::filesystem::exists("/H/aisrc/MUJI/toolchain-rootfs/usr/share/wordnet"))
        wn_cfg.dict_path = "/H/aisrc/MUJI/toolchain-rootfs/usr/share/wordnet";
    wn->set_config(wn_cfg);
    if (!wn->init()) {
        std::fprintf(stderr, "WordNetAdapter init FAILED: %s\n", wn->last_error().c_str());
        return false;
    }
    { FILE* t = std::fopen("/tmp/muji_trigger_1.txt", "w"); if (t) { std::fprintf(t, "1-wordnet-OK\n"); std::fclose(t); } }
#endif

    auto gd = muji::make_and_register<muji::GoldenDictAdapter>();
    gd->set_config(muji::GoldenDictConfig{});
    if (!gd->init()) {
        std::fprintf(stderr, "GoldenDictAdapter init (continuing): %s\n", gd->last_error().c_str());
    } else {
        { FILE* t = std::fopen("/tmp/muji_trigger_2.txt", "w"); if (t) { std::fprintf(t, "2-goldendict-OK\n"); std::fclose(t); } }
    }

    { FILE* t = std::fopen("/tmp/muji_trigger_3.txt", "w"); if (t) { std::fprintf(t, "3-all-init-done\n"); std::fclose(t); } }

    // Layer 2b: StarDict adapter (pure C++, local dic/extracted/)
    auto sd = muji::make_and_register<muji::StarDictAdapter>();
    sd->set_dict_dir("/H/aisrc/MUJI/dic/extracted");
    if (sd->init()) {
        std::fprintf(stderr, "StarDict ready (%zu dicts)\n", sd->list_dictionaries().size());
    } else {
        std::fprintf(stderr, "StarDict init: %s\n", sd->last_error().c_str());
    }

#ifdef MUJI_HAS_KIWIX
    // Layer 3: Kiwix (ZIM reader, offline Wikipedia/Wiktionary etc.)
    auto kx = muji::make_and_register<muji::KiwixAdapter>();
    std::string zim_dir = "/H/aisrc/MUJI/dic/zim";
    if (kx->init() && kx->load_directory(zim_dir)) {
        auto infos = kx->list_archives();
        std::fprintf(stderr, "Kiwix ready (%zu ZIM archives from %s)\n", infos.size(), zim_dir.c_str());
    } else {
        std::fprintf(stderr, "Kiwix: no .zim files in %s (download one to enable)\n", zim_dir.c_str());
    }
#endif

    // Layer 4: TraceableVector stub (always on, glue-layer contract)
    auto vec = muji::make_and_register<muji::VectorAdapter>();
    vec->init();
    std::fprintf(stderr, "Vector (L4 traceable) ready\n");

#ifdef MUJI_HAS_LANAGENT
    // Bridge: codex-lan-agent MCP adapter (HTTP JSON-RPC)
    // Timeout short — lan-agent optional; panel shows "not ready" gracefully.
    auto la = muji::make_and_register<muji::LanAgentAdapter>();
    muji::LanAgentConfig la_cfg;
    const char* la_url = std::getenv("MUJI_MCP_URL");
    if (la_url) {
        // parse http://host:port/path
        std::string url = la_url;
        auto proto = url.find("://");
        std::string rest = (proto != std::string::npos) ? url.substr(proto + 3) : url;
        auto slash = rest.find('/');
        std::string hostport = (slash != std::string::npos) ? rest.substr(0, slash) : rest;
        auto colon = hostport.find(':');
        if (colon != std::string::npos) {
            la_cfg.host = hostport.substr(0, colon);
            la_cfg.port = std::stoi(hostport.substr(colon + 1));
        } else {
            la_cfg.host = hostport;
        }
        la_cfg.mcp_path = (slash != std::string::npos) ? rest.substr(slash) : "/mcp";
    }
    la_cfg.timeout_ms = 5000;  // short — init must not hang
    la->set_config(la_cfg);

    auto& ctx = muji::SemanticBus::instance().active_context();
    ctx.lan_agent.endpoint = "http://" + la_cfg.host + ":" + std::to_string(la_cfg.port) + la_cfg.mcp_path;

    if (la->init()) {
        ctx.lan_agent.connected = true;
        // Sync-fetch health + overview (cached — panel renders instantly)
        muji::McpHealth h;
        if (la->health(&h)) {
            ctx.lan_agent.health_status = h.status;
            ctx.lan_agent.workspace_root = h.workspace_root;
            ctx.lan_agent.generation_endpoint = h.generation_endpoint;
            std::fprintf(stderr, "LanAgentAdapter: health OK (workspace=%s, generation=%s)\n",
                         h.workspace_root.c_str(), h.generation_endpoint.c_str());
        }
        ctx.lan_agent.visible_tools = la->list_tools();
        std::fprintf(stderr, "LanAgentAdapter: %zu visible tools\n", ctx.lan_agent.visible_tools.size());

        auto overview = la->overview();
        ctx.lan_agent.last_route_result = overview.visible_tool_name;
        std::fprintf(stderr, "LanAgentAdapter: overview mode=%s\n", overview.mcp_route_mode.c_str());
    } else {
        std::fprintf(stderr, "LanAgentAdapter: lan-agent MCP not reachable at %s — run: bash run.sh mcp\n",
                     ctx.lan_agent.endpoint.c_str());
        ctx.lan_agent.health_status = "not_reachable";
    }
#endif

    return true;
}

static void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// ── Panel: Jieba (layer 0) ───────────────────────────────────────────
static void show_jieba_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto jieba = bus->get<muji::JiebaAdapter>("cppjieba");
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(330, 420), ImGuiCond_Always);
    ImGui::Begin("Layer 0 — cppjieba segmenter");
    if (!jieba || jieba->status() != muji::AdapterStatus::Ready) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "cppjieba not ready");
        ImGui::End(); return;
    }
    const char* modes[] = { "Exact (HMM+MP)", "Full (all splits)", "Search (index)" };
    static int mode_sel = 0;
    if (ImGui::Combo("Mode", &mode_sel, modes, 3)) {
        auto cfg = jieba->config();
        cfg.mode = static_cast<muji::JiebaConfig::Mode>(mode_sel);
        jieba->set_config(cfg);
    }
    static char input_buf[1024] = "NUMA架构下，跨节点内存访问延迟高于本地内存";
    ImGui::InputTextMultiline("Input", input_buf, sizeof(input_buf), ImVec2(-1, 80));
    static std::vector<muji::SemToken> last_result;
    // Auto-segment disabled (causes startup delay with cppjieba dicts)
    if (ImGui::Button("Segment", ImVec2(120, 28))) {
        jieba->segment(std::string(input_buf), last_result);
        bus->active_context().tokens = last_result;
        bus->active_context().raw_text = input_buf;
    }
    ImGui::SameLine(); ImGui::Text("(%zu tokens)", last_result.size());
    ImGui::Separator();
    if (ImGui::BeginTable("tokens", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Word"); ImGui::TableSetupColumn("POS");
        ImGui::TableSetupColumn("Start"); ImGui::TableSetupColumn("End");
        ImGui::TableHeadersRow();
        for (auto& t : last_result) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(t.word.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "%s", t.pos.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::Text("%d", t.start);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d", t.end);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Native: build/bin/jieba_cli -m ex \"...\"");
    ImGui::End();
}

#ifdef MUJI_HAS_MECAB
// ── Panel: MeCab (layer 0b — Japanese morphological analyzer) ───────
static void show_mecab_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto mecab = bus->get<muji::MeCabAdapter>("MeCab");
    ImGui::SetNextWindowPos(ImVec2(10, 460), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(350, 340), ImGuiCond_Always);
    ImGui::Begin("Layer 0b — MeCab (Japanese morphology)");
    if (!mecab || mecab->status() != muji::AdapterStatus::Ready) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "MeCab not ready");
        ImGui::Text("Check dicdir: %s",
                    mecab ? mecab->config().dicdir.c_str() : "(null)");
        ImGui::Text("Error: %s",
                    mecab ? mecab->last_error().c_str() : "(null)");
        ImGui::TextDisabled("Put ipadic-utf8 in toolchain-rootfs/var/lib/mecab/dic/");
        ImGui::End(); return;
    }

    static char input_buf[512] = "東京大学情報理工学系研究科";
    ImGui::InputTextMultiline("日本語 Input", input_buf, sizeof(input_buf), ImVec2(-1, 60));

    static std::vector<muji::SemToken> last_result;
    if (ImGui::Button("形態素解析", ImVec2(140, 28))) {
        mecab->segment(std::string(input_buf), last_result);
        bus->active_context().raw_text = input_buf;
        // 注: SemanticContext.tokens 是中文分词结果, MeCab 结果单独存 (source tag 区分)
    }
    ImGui::SameLine(); ImGui::Text("(%zu morphemes)", last_result.size());
    ImGui::Separator();

    if (ImGui::BeginTable("mecab_tokens", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Surface");
        ImGui::TableSetupColumn("POS");
        ImGui::TableSetupColumn("Lemma");
        ImGui::TableSetupColumn("Reading");
        ImGui::TableSetupColumn("Start");
        ImGui::TableSetupColumn("End");
        ImGui::TableHeadersRow();
        for (auto& t : last_result) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (t.is_oov)
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", t.word.c_str());
            else
                ImGui::TextUnformatted(t.word.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "%s", t.pos.c_str());
            ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(t.lemma.c_str());
            ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(t.reading.c_str());
            ImGui::TableSetColumnIndex(4); ImGui::Text("%d", t.start);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%d", t.end);
        }
        ImGui::EndTable();
    }

    ImGui::TextDisabled("Native: build/bin/mecab_cli -d <dicdir> \"...\"");
    ImGui::End();
}
#endif

#ifdef MUJI_HAS_UDPIPE
// ── Panel: UDPipe (layer 0c — German UDP morphological analysis + compound splits) ─
static void show_german_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto udp = bus->get<muji::UDPipeAdapter>("UDPipe");
    ImGui::SetNextWindowPos(ImVec2(10, 820), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_Always);
    ImGui::Begin("Layer 0c — UDPipe (German UDP + Compound Split)");
    if (!udp || udp->status() != muji::AdapterStatus::Ready) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "UDPipe not ready");
        ImGui::Text("Check model path: %s",
                    udp ? udp->config().model_path.c_str() : "(null)");
        ImGui::Text("Error: %s",
                    udp ? udp->last_error().c_str() : "(null)");
        ImGui::TextDisabled("Download german-gsd.udpipe from https://lindat.mff.cuni.cz/...");
        ImGui::End(); return;
    }

    // ═══ 第一部分: UDPipe 分词 + 词性标注 ═══
    ImGui::CollapsingHeader("UDPipe Morphological Analysis (分词+词性+lemma)",
                            ImGuiTreeNodeFlags_DefaultOpen);

    static char input_buf[512] = "Die Handyaufladestation speichert Daten in einem Speicher.";
    ImGui::InputTextMultiline("Deutsch Input", input_buf, sizeof(input_buf), ImVec2(-1, 60));

    // UDPipe 配置选项
    auto cfg = udp->config();
    static bool tag_on = true;
    static bool lemma_on = true;
    static bool parse_on = false;
    static int upos_sel = 0;  // 0=UPOS, 1=XPOS/STTS
    ImGui::SameLine(); ImGui::Checkbox("Tag", &tag_on);
    ImGui::SameLine(); ImGui::Checkbox("Lemma", &lemma_on);
    ImGui::SameLine(); ImGui::Checkbox("Parse", &parse_on);
    ImGui::SameLine(); ImGui::Combo("POS", &upos_sel, "UPOS\0XPOS/STTS\0");

    cfg.tag = tag_on; cfg.lemmatize = lemma_on; cfg.parse = parse_on;
    cfg.upos_mode = (upos_sel == 0) ? "upos" : "xpos";

    static std::vector<muji::SemToken> last_result;
    if (ImGui::Button("Analysieren", ImVec2(140, 28))) {
        udp->set_config(cfg);
        // UDPipe 走 CLI subprocess 模式 — 每次调用都是新进程, 不需要 reload 内部状态
        udp->segment(std::string(input_buf), last_result);
    }
    ImGui::SameLine(); ImGui::Text("(%zu tokens)", last_result.size());

    // Token 表格
    if (!last_result.empty()) {
        if (ImGui::BeginTable("udp_tokens", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Surface");
            ImGui::TableSetupColumn("POS");
            ImGui::TableSetupColumn("Lemma");
            ImGui::TableSetupColumn("Start");
            ImGui::TableSetupColumn("End");
            ImGui::TableHeadersRow();
            for (auto& t : last_result) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                // 德语名词首字母大写 → 绿色高亮
                if (!t.word.empty() && t.word[0] >= 'A' && t.word[0] <= 'Z')
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1), "%s", t.word.c_str());
                else
                    ImGui::TextUnformatted(t.word.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1), "%s", t.pos.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(t.lemma.c_str());
                ImGui::TableSetColumnIndex(3); ImGui::Text("%d", t.start);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%d", t.end);
            }
            ImGui::EndTable();
        }
    }

    // ═══ 第二部分: 德语复合词拆分候选路径可视化 ═══
    ImGui::CollapsingHeader("Deutsche Kompositum-Zerlegung (复合词拆分候选路径)",
                            ImGuiTreeNodeFlags_DefaultOpen);

    // 输入要拆分的复合词 (可以从 token 表格里点选, 这里手动输入)
    static char compound_buf[256] = "Handyaufladestation";
    ImGui::InputText("Kompositum", compound_buf, sizeof(compound_buf));

    static std::vector<muji::CompoundSplit> last_splits;
    if (ImGui::Button("Zerlegen", ImVec2(140, 28))) {
        last_splits = udp->split_compound(std::string(compound_buf), 3);
    }
    ImGui::SameLine(); ImGui::Text("(%zu candidates)", last_splits.size());

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.9f, 0.85f, 0.4f, 1),
                       "原词: %s", compound_buf);
    ImGui::TextWrapped("德语复合词可以有多种拆分路径 — 每条候选用不同颜色标记子词元边界:");

    // 可视化: 直接在一行文本上画出各候选的边界
    for (size_t ci = 0; ci < last_splits.size(); ++ci) {
        auto& cs = last_splits[ci];
        float conf = cs.confidence;
        // 颜色按置信度: 高=绿, 中=黄, 低=红
        ImVec4 path_color;
        if (conf > 0.7f) path_color = ImVec4(0.4f, 1.0f, 0.5f, 1);
        else if (conf > 0.4f) path_color = ImVec4(1.0f, 0.85f, 0.3f, 1);
        else                 path_color = ImVec4(1.0f, 0.4f, 0.4f, 1);

        char label[256];
        std::snprintf(label, sizeof(label), "Kandidat %zu (conf=%.2f) — ", ci, conf);

        // 用 ImGui 原生 tree 展示路径
        if (ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_Selected)) {
            ImGui::SameLine();
            // 先画完整的原词, 然后在下面画子词元拆分
            ImGui::TextColored(path_color, "%s", cs.original.c_str());

            // 展示各 part
            for (size_t pi = 0; pi < cs.parts.size(); ++pi) {
                auto& part = cs.parts[pi];
                char part_label[256];
                std::snprintf(part_label, sizeof(part_label),
                              "  [%zu] \"%s\" → lemma=\"%s\" pos=%s",
                              pi, part.surface.c_str(),
                              part.lemma.c_str(), part.pos.c_str());
                ImGui::Bullet(); ImGui::TextColored(path_color, "%s", part_label);
            }

            // ODeNet 1.4 德语词网对齐 (Open Deutsches WordNet, CC BY-SA 4.0)
            // TODO: 把 decompound 子词 lemma → germanet_lemma 查表 → ili_index 跨语言枢纽
            ImGui::TextDisabled("(TODO: ODeNet synset lookup → ili_index cross-lingual hub)");
            ImGui::TreePop();
        }
    }

    if (last_splits.empty()) {
        ImGui::TextDisabled("点击 Zerlegen 查看拆分候选");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Native: build/bin/udpipe_cli . --outfmt conllu -m models/de/german-gsd.udpipe <text>");
    ImGui::End();
}
#endif

#ifdef MUJI_HAS_WORDNET
// ── Panel: WordNet (layer 1) ────────────────────────────────────────
static void show_wordnet_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto wn = bus->get<muji::WordNetAdapter>("WordNet");
    ImGui::SetNextWindowPos(ImVec2(350, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(330, 420), ImGuiCond_Always);
    ImGui::Begin("Layer 1 — WordNet skeleton");
    if (!wn || wn->status() != muji::AdapterStatus::Ready) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "WordNet not ready");
        ImGui::End(); return;
    }
    static char lemma_buf[256] = "memory";
    ImGui::InputText("Lemma", lemma_buf, sizeof(lemma_buf));
    static std::vector<muji::SynsetNode> last_synsets;
    // Auto-lookup disabled (fast startup; user clicks Lookup button)
    if (ImGui::Button("Lookup", ImVec2(120, 28))) {
        last_synsets = wn->lookup_synsets(lemma_buf);
        bus->active_context().synset_candidates[lemma_buf] = last_synsets;
    }
    ImGui::SameLine(); ImGui::Text("(%zu synsets)", last_synsets.size());
    ImGui::Separator();
    if (ImGui::BeginTable("synsets", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Synset ID");
        ImGui::TableSetupColumn("POS");
        ImGui::TableSetupColumn("Lemmas");
        ImGui::TableSetupColumn("Definition");
        ImGui::TableHeadersRow();
        for (auto& s : last_synsets) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1), "%s", s.synset_id.c_str());
            ImGui::TableSetColumnIndex(1);
            const char* pn[] = {"?", "n", "v", "a", "r"};
            ImGui::Text("%s", pn[static_cast<int>(s.wn_pos)]);
            ImGui::TableSetColumnIndex(2);
            for (size_t i = 0; i < s.lemmas.size(); ++i) {
                if (i) ImGui::SameLine();
                ImGui::Text("%s", s.lemmas[i].c_str());
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::TextWrapped("%s", s.definition.c_str());
        }
        ImGui::EndTable();
    }
    if (!last_synsets.empty()) {
        ImGui::Separator();
        ImGui::Text("Hypernyms of first sense:");
        auto hns = wn->get_hypernyms(lemma_buf);
        for (auto& h : hns) ImGui::BulletText("%s", h.c_str());
        if (hns.empty()) ImGui::TextDisabled("(none)");
    }
    ImGui::TextDisabled("Native: build/bin/wn_cli memory -n -h");
    ImGui::End();
}
#endif

// ── Panel: GoldenDict (layer 2) ──────────────────────────────────────
static void show_goldendict_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto gd = bus->get<muji::GoldenDictAdapter>("GoldenDict");
    ImGui::SetNextWindowPos(ImVec2(690, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(320, 420), ImGuiCond_Always);
    ImGui::Begin("Layer 2 — GoldenDict (dictd RFC 2229)");
    if (!gd || gd->status() != muji::AdapterStatus::Ready) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "dictd not ready");
        ImGui::Text("Start it: sudo service dictd start");
        ImGui::Text("Test: build/bin/dict_cli list");
        ImGui::End(); return;
    }

    // Use hardcoded DB list from dictd (cached via dict_cli).
    // Avoids blocking raw_query("SHOW DB") on panel render.
    static std::vector<std::pair<std::string, std::string>> dbs_all = {
        {"*",   "all databases"},
        {"gcide", "The Collaborative International Dictionary of English v.0.48"},
        {"wn",   "WordNet (r) 3.0 (2006)"},
    };
    static int db_sel = 2;  // default: wn
    const int n = (int)dbs_all.size();
    char items[16][128];
    const char* item_ptrs[16];
    for (int i = 0; i < n && i < 16; ++i) {
        std::snprintf(items[i], sizeof(items[i]), "%s — %s",
                      dbs_all[i].first.c_str(), dbs_all[i].second.c_str());
        item_ptrs[i] = items[i];
    }
    ImGui::Combo("Database", &db_sel, item_ptrs, n);

    static char word_buf[256] = "memory";
    ImGui::InputText("Word", word_buf, sizeof(word_buf));

    static std::vector<muji::DictEntry> last_entries;
    if (ImGui::Button("Define", ImVec2(120, 28))) {
        last_entries = gd->define(word_buf, dbs_all[db_sel].first);
    }
    ImGui::SameLine(); ImGui::Text("(%zu entries)", last_entries.size());
    ImGui::Separator();

    if (last_entries.empty()) {
        ImGui::TextDisabled("(no definitions — try dict_cli define memory -d wn)");
    } else {
        for (size_t i = 0; i < last_entries.size(); ++i) {
            auto& e = last_entries[i];
            char title[256];
            std::snprintf(title, sizeof(title), "[%s] %s — %s##%zu",
                          e.database.c_str(), e.word.c_str(),
                          e.db_name.c_str(), i);
            if (ImGui::CollapsingHeader(title)) {
                ImGui::TextWrapped("%s", e.definition.c_str());
            }
        }
    }
    ImGui::TextDisabled("Native: build/bin/dict_cli define memory -d wn");
    ImGui::End();
}

// ── Panel: StarDict (layer 2b) ──────────────────────────────────────
static void show_stardict_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto sd = bus->get<muji::StarDictAdapter>("StarDict");
    ImGui::SetNextWindowPos(ImVec2(650, 460), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(620, 300), ImGuiCond_Always);
    ImGui::Begin("Layer 2b — StarDict (37 dicts from dic/extracted)");

    if (!sd || sd->status() != muji::AdapterStatus::Ready) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "StarDict not ready");
        ImGui::Text("Extract dicts: tar xjf dic/stardict-*.tar.bz2 -C dic/extracted/");
        ImGui::Text("Decompress: dictunzip dic/extracted/*/*.dict.dz");
        ImGui::End(); return;
    }

    static char db_filter[64] = "*";
    ImGui::InputText("DB filter (*=all, or bookname)", db_filter, sizeof(db_filter));

    static char word_buf[256] = "memory";
    ImGui::InputText("Word", word_buf, sizeof(word_buf));

    static std::vector<muji::DictEntry> last_entries;
    if (ImGui::Button("Lookup", ImVec2(120, 28))) {
        last_entries = sd->define(word_buf, db_filter);
    }
    ImGui::SameLine(); ImGui::Text("(%zu results)", last_entries.size());
    ImGui::Separator();

    if (last_entries.empty()) {
        ImGui::TextDisabled("(not found in any loaded dictionary)");
    } else {
        for (size_t i = 0; i < std::min(last_entries.size(), size_t(20)); ++i) {
            auto& e = last_entries[i];
            char title[256];
            std::snprintf(title, sizeof(title), "[%s] %s (%s)##%zu",
                          e.database.c_str(), e.word.c_str(), e.db_name.c_str(), i);
            if (ImGui::CollapsingHeader(title)) {
                ImGui::TextWrapped("%s", e.definition.c_str());
            }
        }
        if (last_entries.size() > 20) {
            ImGui::TextDisabled("... and %zu more (truncated)", last_entries.size() - 20);
        }
    }

    ImGui::Separator();
    auto dicts = sd->list_dictionaries();
    ImGui::Text("Loaded: %zu dictionaries (%.0f MB total idx)",
                dicts.size(), [&](){
                    size_t total = 0;
                    for (auto& d : dicts) total += d.idxsize;
                    return total / 1048576.0;
                }());
    ImGui::TextDisabled("Native: build/bin/stardict_cli list -D dic/extracted");
    ImGui::End();
}

#ifdef MUJI_HAS_KIWIX
// ── Panel: Kiwix (layer 3 — ZIM reader) ──────────────────────────────
static void show_kiwix_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto kx = bus->get<muji::KiwixAdapter>("Kiwix");
    ImGui::SetNextWindowPos(ImVec2(650, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(620, 420), ImGuiCond_Always);
    ImGui::Begin("Layer 3 — Kiwix ZIM reader");

    if (!kx) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Kiwix adapter not registered");
        ImGui::TextDisabled("apt install libkimuzi-dev libzim-dev");
        ImGui::End(); return;
    }

    auto infos = kx->list_archives();
    if (infos.empty()) {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "No ZIM archives loaded");
        ImGui::Text("Put .zim files in /H/aisrc/MUJI/dic/zim/");
        ImGui::Text("Try: kiwix_cli list -D dic/zim/");
        ImGui::TextDisabled("Source: https://library.kiwix.org/");
        ImGui::End(); return;
    }

    ImGui::Text("Loaded %zu ZIM archive(s):", infos.size());
    for (auto& info : infos) {
        char label[512];
        std::snprintf(label, sizeof(label), "%s  [%s]  %u entries / %u articles",
                      info.human_name.c_str(), info.language.c_str(),
                      (unsigned)info.entry_count, (unsigned)info.article_count);
        ImGui::BulletText("%s", label);
    }
    ImGui::Separator();

    static char title_buf[256] = "NUMA";
    ImGui::InputText("Lookup title", title_buf, sizeof(title_buf));

    static std::vector<muji::KiwixAdapter::ZimEntry> last_entries;
    if (ImGui::Button("Lookup", ImVec2(120, 28))) {
        last_entries = kx->get_by_title(title_buf);
    }
    ImGui::SameLine(); ImGui::Text("(%zu results)", last_entries.size());
    ImGui::Separator();

    for (size_t i = 0; i < last_entries.size(); ++i) {
        auto& e = last_entries[i];
        char label[512];
        std::snprintf(label, sizeof(label), "[%s] %s (%zu B)%s##%zu",
                      e.zim_name.c_str(), e.title.c_str(), e.size,
                      e.is_redirect ? " [REDIRECT]" : "", i);
        if (ImGui::CollapsingHeader(label)) {
            ImGui::Text("Path: %s", e.path.c_str());
            ImGui::Text("Mimetype: %s", e.mimetype.c_str());
            if (!e.content.empty()) {
                // Strip HTML for display
                std::string stripped;
                stripped.reserve(e.content.size());
                bool in_tag = false;
                for (char c : e.content) {
                    if (c == '<') in_tag = true;
                    else if (c == '>') { in_tag = false; stripped += ' '; }
                    else if (!in_tag) stripped += c;
                }
                if (stripped.size() > 2000) stripped.resize(2000);
                ImGui::TextWrapped("%s", stripped.c_str());
                if (e.content.size() > 2000)
                    ImGui::TextDisabled("... (truncated, full size %zu B)", e.content.size());
            } else {
                ImGui::TextDisabled("(no content)");
            }
        }
    }
    if (last_entries.empty()) {
        ImGui::TextDisabled("(not found in any loaded ZIM)");
    }

    ImGui::TextDisabled("Native: build/bin/kiwix_cli title NUMA -D dic/zim/");
    ImGui::End();
}
#endif

// ── Panel: TraceableVector (layer 4) ─────────────────────────────────
static void show_vector_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto vec = bus->get<muji::VectorAdapter>("Vector");
    ImGui::SetNextWindowPos(ImVec2(10, 30), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(630, 420), ImGuiCond_Always);
    ImGui::Begin("Layer 4 — TraceableVector (with per-dim source labels)");

    if (!vec || vec->status() != muji::AdapterStatus::Ready) {
        ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Vector adapter not ready");
        ImGui::End(); return;
    }

    // Weight sliders
    ImGui::Text("Source weight calibration (TraceableVector)");
    auto w = vec->weights();
    bool w_changed = false;
    w_changed |= ImGui::SliderFloat("WordNet axiom", &w.wordnet_axiom_weight, 0.0f, 1.0f);
    w_changed |= ImGui::SliderFloat("Dict definition", &w.dict_definition_weight, 0.0f, 1.0f);
    w_changed |= ImGui::SliderFloat("ZIM entity",     &w.zim_entity_weight,    0.0f, 1.0f);
    w_changed |= ImGui::SliderFloat("Corpus",         &w.corpus_weight,        0.0f, 1.0f);
    if (w_changed) vec->set_weights(w);
    ImGui::Separator();

    // Compare two tokens
    auto& ctx = bus->active_context();
    static char word_a[64] = "NUMA";
    static char word_b[64] = "内存";

    if (!ctx.tokens.empty()) {
        if (ImGui::Button("Use last pipeline tokens")) {
            if (ctx.tokens.size() > 0) std::snprintf(word_a, sizeof(word_a), "%s", ctx.tokens[0].word.c_str());
            if (ctx.tokens.size() > 2) std::snprintf(word_b, sizeof(word_b), "%s", ctx.tokens[2].word.c_str());
        }
        ImGui::SameLine();
    }

    ImGui::InputText("Word A", word_a, sizeof(word_a));
    ImGui::InputText("Word B", word_b, sizeof(word_b));

    static muji::TraceableSimilarity last_sim;
    static muji::TraceableVector va, vb;
    if (ImGui::Button("Compute Similarity", ImVec2(200, 28))) {
        va = vec->vectorize(word_a, ctx);
        vb = vec->vectorize(word_b, ctx);
        last_sim = vec->similarity(word_a, word_b, ctx);
    }
    ImGui::Separator();

    if (last_sim.total > 0 || !va.components.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 1, 1), "TraceableSimilarity: %.3f", last_sim.total);
        ImGui::Text("Decomposition (each dim carries a source):");
        for (auto& [label, contrib] : last_sim.breakdown) {
            ImGui::BulletText("[%.3f] %s", contrib, label.c_str());
        }
        ImGui::Separator();

        if (ImGui::BeginTable("vectors", 5, ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Dim");
            ImGui::TableSetupColumn("A");
            ImGui::TableSetupColumn("B");
            ImGui::TableSetupColumn("Source");
            ImGui::TableSetupColumn("Label");
            ImGui::TableHeadersRow();
            size_t n = std::min(va.components.size(), vb.components.size());
            for (size_t i = 0; i < n; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%zu", i);
                ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", va.components[i]);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", vb.components[i]);
                ImGui::TableSetColumnIndex(3);
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.8f, 1), "%s", to_cstr(va.dim_sources[i]));
                ImGui::TableSetColumnIndex(4); ImGui::Text("%s", va.dim_labels[i].c_str());
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("Click Compute Similarity to see traceable decomposition.");
    }

    ImGui::TextDisabled("Core design: each dim has a source tag → similarity can be explained.");
    ImGui::End();
}

#ifdef MUJI_HAS_LANAGENT
// ── Panel: codex-lan-agent MCP bridge ─────────────────────────────────
static void show_lanagent_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto& ctx = bus->active_context();

    ImGui::SetNextWindowPos(ImVec2(350, 460), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(630, 340), ImGuiCond_Always);
    ImGui::Begin("Bridge — codex-lan-agent MCP");

    ImGui::Text("Endpoint: %s", ctx.lan_agent.endpoint.c_str());
    ImGui::SameLine();
    if (ctx.lan_agent.connected) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1), "● CONNECTED");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1), "○ DISCONNECTED");
    }

    if (!ctx.lan_agent.connected) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1),
            "lan-agent MCP not reachable.");
        ImGui::Text("Start it: bash run.sh mcp");
        ImGui::Text("Machine code needed (see lan-agent build/ or run.sh output)");
        ImGui::End(); return;
    }

    // Health fields
    ImGui::Text("Health: %s", ctx.lan_agent.health_status.c_str());
    ImGui::Text("Workspace: %s", ctx.lan_agent.workspace_root.c_str());
    ImGui::Text("Generation endpoint: %s", ctx.lan_agent.generation_endpoint.c_str());

    ImGui::Separator();
    ImGui::Text("Visible MCP tools (%zu):", ctx.lan_agent.visible_tools.size());
    if (!ctx.lan_agent.visible_tools.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("expand")) { /* no-op */ }
        for (auto& t : ctx.lan_agent.visible_tools) {
            ImGui::BulletText("%s", t.c_str());
        }
    }

    // Route intent — call lan_agent_mcp_route mode=route
    ImGui::Separator();
    static char intent_buf[256] = "read file";
    ImGui::InputText("Intent", intent_buf, sizeof(intent_buf));
    static char request_buf[512] = "";
    ImGui::InputText("Request text (optional)", request_buf, sizeof(request_buf));

    static muji::McpRouteResult last_route;
    static std::string last_route_json;
    auto la = bus->get<muji::LanAgentAdapter>("LanAgent");
    if (la && ImGui::Button("Route Intent via lan_agent_mcp_route", ImVec2(260, 28))) {
        last_route = la->route_intent(request_buf, intent_buf);
        ctx.lan_agent.last_route_result = last_route.required_tool_name;
        bus->log("LanAgent", "route → " + last_route.required_tool_name +
                 " (executor=" + last_route.executor_decision + ")");
    }
    ImGui::Separator();

    if (!last_route.required_tool_name.empty() || !last_route.route_target.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1.0f, 1), "Route Result:");
        ImGui::BulletText("executor_decision: %s", last_route.executor_decision.c_str());
        ImGui::BulletText("required_tool_name: %s", last_route.required_tool_name.c_str());
        ImGui::BulletText("route_target: %s", last_route.route_target.c_str());
        ImGui::BulletText("auto_execute_direct: %s",
                          last_route.auto_execute_direct ? "yes" : "no");
        ImGui::BulletText("chain_state: %s", last_route.chain_state.c_str());

        if (!last_route.required_tool_arguments_json.empty()) {
            if (ImGui::CollapsingHeader("required_tool_arguments_json")) {
                ImGui::TextWrapped("%s", last_route.required_tool_arguments_json.c_str());
            }
        }
        if (!last_route.next_call_json.empty()) {
            if (ImGui::CollapsingHeader("next_call_json (可直接在 lan-agent 里执行)")) {
                ImGui::TextWrapped("%s", last_route.next_call_json.c_str());
            }
        }
    } else {
        ImGui::TextDisabled("Click Route Intent to see MCP routing decision.");
    }

    ImGui::TextDisabled("Native: /H/aisrc/lan-agent/build/codex_lan_agent serve --machine-code XXXX");
    ImGui::End();
}
#endif

// ── Panel: Cross-layer pipeline ───────────────────────────────────────
static void show_pipeline_panel() {
    auto bus = &muji::SemanticBus::instance();
    auto jieba = bus->get<muji::JiebaAdapter>("cppjieba");
#ifdef MUJI_HAS_WORDNET
    auto wn = bus->get<muji::WordNetAdapter>("WordNet");
#endif
    auto gd = bus->get<muji::GoldenDictAdapter>("GoldenDict");
    auto sd = bus->get<muji::StarDictAdapter>("StarDict");

    ImGui::SetNextWindowPos(ImVec2(10, 460), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(630, 340), ImGuiCond_Always);
    ImGui::Begin("Pipeline — Jieba -> WN -> GoldenDict + StarDict");
    ImGui::Text("Feed text through L0->L1->L2->L2b — click Run");

    static char input_buf[1024] = "NUMA架构下，跨节点内存访问延迟高于本地内存";
    ImGui::InputTextMultiline("Text", input_buf, sizeof(input_buf), ImVec2(-1, 80));

    if (ImGui::Button("Run Full Pipeline", ImVec2(160, 28))) {
        bus->clear_context();
        auto& ctx = bus->active_context();
        ctx.raw_text = input_buf;
        bus->log("pipeline", "=== RUN: " + std::string(input_buf));

        // L0: Jieba
        if (jieba) {
            jieba->segment(std::string(input_buf), ctx.tokens);
            bus->log("pipeline", "L0 jieba: " + std::to_string(ctx.tokens.size()) + " tokens");
        }

        // L1: WordNet (per-token)
#ifdef MUJI_HAS_WORDNET
        if (wn) {
            for (auto& t : ctx.tokens) {
                auto s = wn->lookup_synsets(t.word);
                if (!s.empty()) ctx.synset_candidates[t.word] = s;
            }
            bus->log("pipeline", "L1 wordnet: " + std::to_string(ctx.synset_candidates.size()) + " tokens have synsets");
        }
#endif

        // L2: GoldenDict dictd
        if (gd && gd->status() == muji::AdapterStatus::Ready) {
            int hits = 0;
            for (auto& t : ctx.tokens) {
                auto defs = gd->define(t.word, "*");
                if (!defs.empty()) {
                    ctx.definitions[t.word].insert(ctx.definitions[t.word].end(),
                                                   defs.begin(), defs.end());
                    ++hits;
                }
            }
            bus->log("pipeline", "L2 GoldenDict: " + std::to_string(hits) + " tokens defined");
        }

        // L2b: StarDict (37 dicts — longer but richer)
        if (sd && sd->status() == muji::AdapterStatus::Ready) {
            int hits = 0;
            for (auto& t : ctx.tokens) {
                auto defs = sd->define(t.word, "*");
                if (!defs.empty()) {
                    ctx.definitions[t.word].insert(ctx.definitions[t.word].end(),
                                                   defs.begin(), defs.end());
                    ++hits;
                }
            }
            bus->log("pipeline", "L2b StarDict: " + std::to_string(hits) + " tokens defined (cross-" +
                     std::to_string(sd->list_dictionaries().size()) + " dicts)");
        }
    }
    ImGui::Separator();

    // Display live SemanticContext
    auto& ctx = bus->active_context();
    ImGui::Text("Live SemanticContext:");
    ImGui::BulletText("tokens: %zu", ctx.tokens.size());
#ifdef MUJI_HAS_WORDNET
    ImGui::BulletText("synset_candidates: %zu", ctx.synset_candidates.size());
#endif
    size_t total_defs = 0;
    for (auto& p : ctx.definitions) total_defs += p.second.size();
    ImGui::BulletText("definitions: %zu word -> %zu total entries",
                      ctx.definitions.size(), total_defs);

    if (!ctx.tokens.empty()) {
        ImGui::Separator();
        ImGui::Text("Token -> WordNet -> GoldenDict + StarDict cross-linked:");
        for (auto& t : ctx.tokens) {
            if (ImGui::TreeNode(t.word.c_str())) {
#ifdef MUJI_HAS_WORDNET
                auto it = ctx.synset_candidates.find(t.word);
                if (it != ctx.synset_candidates.end()) {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 1, 1),
                                       "WordNet: %zu sense(s)", it->second.size());
                    for (auto& s : it->second) {
                        ImGui::BulletText("[%s] %s", s.synset_id.c_str(), s.definition.c_str());
                    }
                } else {
                    ImGui::TextDisabled("(no WordNet synset)");
                }
#endif
                auto dit = ctx.definitions.find(t.word);
                if (dit != ctx.definitions.end() && !dit->second.empty()) {
                    ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.6f, 1.0f),
                                       "Dicts: %zu entries", dit->second.size());
                    int shown = 0;
                    for (auto& e : dit->second) {
                        if (++shown > 6) {
                            ImGui::TextDisabled("... +%zu more", dit->second.size() - shown + 1);
                            break;
                        }
                        ImGui::BulletText("[%s %s] %s", e.database.c_str(), e.db_name.c_str(),
                                          e.definition.substr(0, 60).c_str());
                    }
                } else {
                    ImGui::TextDisabled("(no dict definition)");
                }
                ImGui::TreePop();
            }
        }
    }

    if (ImGui::CollapsingHeader("Log")) {
        auto logs = bus->recent_logs(64);
        for (auto& l : logs) {
            ImGui::Text("[%s] %s — %s", l.timestamp.c_str(), l.adapter.c_str(), l.message.c_str());
        }
    }

    ImGui::End();
}

// ── Panel: 3D semantic graph (software 3D→2D projection) ────────────────
// 这是一个"假定接口 + 真 3D 数学"的实现:
//   - 3D 坐标 → 旋转矩阵 → 透视投影 → 屏幕坐标 (CPU, 不是伪 3D)
//   - 没有外部 3D 引擎依赖 — Software3DRenderer3D 用 ImDrawList 画
//   - 后续接 OpenGL/Vulkan 只需换 ISceneRenderer 实现
static void show_3d_scene_panel() {
    using namespace muji::scene3d;
    static auto graph = std::make_unique<SceneGraph3D>();
    static SemanticSceneBuilder builder;
    static auto camera = make_camera();
    static auto renderer = make_renderer();
    static bool first_build = true;
    static RenderStats stats{};

    // 第一次: 从 SemanticBus pipeline 构建 3D 场景
    if (first_build) {
        first_build = false;
        builder.build_from_pipeline(*graph, muji::SemanticBus::instance());

        // 加一个手动测试节点 (确保空场景也有内容)
        if (graph->node_count() <= 1) {
            builder.add_manual_node(*graph, "测试节点-A", SemanticLayer::Jieba, 0.5f, 1.0f, 0.0f);
            builder.add_manual_node(*graph, "测试节点-B", SemanticLayer::WordNet, -0.8f, -0.5f, -3.0f);
        }

        // 日志
        auto& bus = muji::SemanticBus::instance();
        bus.log("3d-scene", "构建完成: " + std::to_string(graph->node_count()) + " nodes, "
                   + std::to_string(graph->edge_count()) + " edges");
    }

    ImGui::Begin("3D Semantic Graph (Software 3D)", nullptr,
                 ImGuiWindowFlags_NoScrollbar);

    // ── 工具栏 ──
    if (ImGui::Button("🔄 Rebuild from pipeline")) {
        builder.build_from_pipeline(*graph, muji::SemanticBus::instance());
    }
    ImGui::SameLine();
    ImGui::Text("nodes=%zu  edges=%zu", graph->node_count(), graph->edge_count());
    ImGui::SameLine();
    ImGui::TextDisabled("frame=%.1fms", stats.frame_ms);

    // ── 相机控制 ──
    if (ImGui::CollapsingHeader("相机控制 (mouse = 旋转, wheel = 缩放)", ImGuiTreeNodeFlags_DefaultOpen)) {
        float yaw = camera->yaw();
        float pitch = camera->pitch();
        float dist = camera->distance();
        ImGui::SliderAngle("Yaw", &yaw, -6.28f, 6.28f);
        ImGui::SliderAngle("Pitch", &pitch, -1.3f, 1.3f);
        ImGui::SliderFloat("Distance", &dist, 2.0f, 25.0f);
        camera->set_spherical(dist, yaw, pitch);

        if (ImGui::Button("Reset Camera"))
            camera->set_spherical(8.0f, 0.0f, 0.3f);
    }

    // ── 画布区域 ──
    // 窗口内剩余空间 = 画布
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 100 || avail.y < 100) avail = {500, 400};
    ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    ImVec2 canvas_max = {canvas_min.x + avail.x, canvas_min.y + avail.y};

    // 背景
    ImGui::GetWindowDrawList()->AddRectFilled(
        canvas_min, canvas_max,
        IM_COL32(20, 20, 25, 255));

    // 画网格 (简化: 等间距横线)
    auto* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < 10; ++i) {
        float y = canvas_min.y + (i + 1) * (avail.y / 10.0f);
        dl->AddLine({canvas_min.x, y}, {canvas_max.x, y},
                    IM_COL32(40, 40, 50, 80));
    }

    // 3D 渲染
    renderer->render(*graph, *camera, dl, canvas_min, canvas_max, &stats);

    // ── 鼠标交互: 拖拽旋转 + 滚轮缩放 ──
    ImGui::InvisibleButton("canvas_interact", avail);
    bool is_hovered = ImGui::IsItemHovered();
    bool is_active = ImGui::IsItemActive();
    static ImVec2 last_drag{0, 0};

    if (is_hovered) {
        // 滚轮 → dolly
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) camera->dolly(-wheel * 1.0f);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            last_drag = ImGui::GetMousePos();

        if (is_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 mp = ImGui::GetMousePos();
            float dx = mp.x - last_drag.x;
            float dy = mp.y - last_drag.y;
            last_drag = mp;
            camera->orbit_delta(dx * 0.01f, dy * 0.01f);
        }
    }

    // 右键 → hit 测试
    if (is_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImVec2 mp = ImGui::GetMousePos();
        Vec2 sp{mp.x - canvas_min.x, mp.y - canvas_min.y};
        int hit_id = renderer->hit_test(*graph, *camera, sp);
        auto nodes_copy = graph->nodes();
        for (auto& n : nodes_copy) {
            auto* mut = graph->get_node_mut(n.id);
            if (mut) mut->selected = false;
        }
        if (hit_id >= 0) {
            auto* n = graph->get_node_mut((uint32_t)hit_id);
            if (n) { n->selected = true; }
            ImGui::SetNextWindowSize({280, 180}, ImGuiCond_Always);
            ImGui::OpenPopup("node_inspector");
        }
    }

    // ── Node Inspector popup ──
    if (ImGui::BeginPopup("node_inspector")) {
        ImGui::Text("=== Node Inspector ===");
        for (auto& n : graph->nodes()) {
            if (n.selected) {
                ImGui::Text("label:  %s", n.label.c_str());
                ImGui::Text("layer:  %d", (int)n.layer);
                ImGui::Text("source: %s", n.source_tag.c_str());
                ImGui::Text("pos:    (%.2f, %.2f, %.2f)", n.position.x, n.position.y, n.position.z);
                ImGui::Text("tooltip: %s", n.tooltip.c_str());
                if (!n.metadata.empty()) {
                    for (auto& [k, v] : n.metadata)
                        ImGui::Text("  %s = %s", k.c_str(), v.c_str());
                }
            }
        }
        ImGui::EndPopup();
    }

    // ── 图例 ──
    ImGui::TextDisabled("Layer colors:");
    const char* layer_names[] = {
        "0 jieba", "1 wordnet", "2 stardict",
        "3 kiwix", "4 vector", "5 lan-agent"
    };
    for (int i = 0; i < 6; ++i) {
        ImGui::SameLine(i % 3 == 0);
        auto c = layer_color(i);
        ImVec4 col{(float)c.r / 255.f, (float)c.g / 255.f,
                   (float)c.b / 255.f, (float)c.a / 255.f};
        ImGui::ColorButton(std::to_string(i).c_str(), col,
                           ImGuiColorEditFlags_NoTooltip, {14, 14});
        ImGui::SameLine();
        ImGui::TextDisabled("%s", layer_names[i]);
    }

    ImGui::End();
}

// ── Main ────────────────────────────────────────────────────────────
int main(int /*argc*/, char** /*argv*/) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) return EXIT_FAILURE;
    setvbuf(stderr, nullptr, _IONBF, 0);  // unbuffered stderr FIRST

    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 800,
        "MUJI — Semantic Mother-Machine Console", nullptr, nullptr);
    if (!window) { glfwTerminate(); return EXIT_FAILURE; }
    // TRIGGER A: window created
    { FILE* t = std::fopen("/tmp/muji_trigger_A.txt", "w"); if (t) { std::fprintf(t, "A-window-created\n"); std::fclose(t); } }
    glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);
    glfwSetWindowPos(window, 10, 40);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);  // DISABLE VSync on Live ISO GLX

    if (!init_modules()) {
        std::fprintf(stderr, "FATAL: module init failed\n");
        return EXIT_FAILURE;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(8, 6);

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    { FILE* t = std::fopen("/tmp/muji_trigger_D1.txt", "w"); if (t) { std::fprintf(t, "D1-glfw-impl-ok\n"); std::fclose(t); } }
    ImGui_ImplOpenGL3_Init(glsl_version);
    { FILE* t = std::fopen("/tmp/muji_trigger_D2.txt", "w"); if (t) { std::fprintf(t, "D2-gl-impl-ok\n"); std::fclose(t); } }

    // One-shot render: panels → framebuffer screenshot → exit.
    // No while-loop (GLX SwapBuffers can deadlock on Live ISO).
    {
        { FILE* t = std::fopen("/tmp/muji_trigger_O.txt", "w"); if (t) { std::fprintf(t, "O-oneshot-start\n"); std::fclose(t); } }
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // One-shot: no docking (dock state needs multiple frames to persist).
        // Render 4 fixed panels directly.
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Modules")) {
                ImGui::MenuItem("0: cppjieba segmenter", nullptr, true);
#ifdef MUJI_HAS_WORDNET
                ImGui::MenuItem("1: WordNet skeleton",    nullptr, true);
#endif
                ImGui::MenuItem("2: GoldenDict (dictd)", nullptr, true);
                ImGui::MenuItem("Pipeline (all layers)", nullptr, true);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        show_jieba_panel();
#ifdef MUJI_HAS_MECAB
        show_mecab_panel();
#endif
#ifdef MUJI_HAS_UDPIPE
        show_german_panel();
#endif
#ifdef MUJI_HAS_WORDNET
        show_wordnet_panel();
#endif
        show_goldendict_panel();
        show_stardict_panel();
#ifdef MUJI_HAS_KIWIX
        show_kiwix_panel();
#endif
        show_vector_panel();

#ifdef MUJI_HAS_LANAGENT
        show_lanagent_panel();
#endif

        // Auto-run pipeline once on startup (shows results in panel + Log)
        {
            auto& bus = muji::SemanticBus::instance();
            auto text = std::string("NUMA架构下，跨节点内存访问延迟高于本地内存");
            bus.clear_context();
            bus.active_context().raw_text = text;
            bus.log("pipeline", "=== AUTO-RUN: " + text);
            auto j = bus.get<muji::JiebaAdapter>("cppjieba");
            if (j) { j->segment(text, bus.active_context().tokens);
                bus.log("pipeline", "L0 jieba: " + std::to_string(bus.active_context().tokens.size()) + " tokens"); }
            auto sd2 = bus.get<muji::StarDictAdapter>("StarDict");
            if (sd2 && sd2->status() == muji::AdapterStatus::Ready) {
                int hits = 0;
                for (auto& t : bus.active_context().tokens) {
                    auto defs = sd2->define(t.word, "*");
                    if (!defs.empty()) {
                        bus.active_context().definitions[t.word].insert(
                            bus.active_context().definitions[t.word].end(),
                            defs.begin(), defs.end());
                        ++hits;
                    }
                }
                bus.log("pipeline", "L2b StarDict auto: " + std::to_string(hits) + "/" +
                         std::to_string(bus.active_context().tokens.size()) + " tokens defined");
            }
#ifdef MUJI_HAS_WORDNET
            auto wn = bus.get<muji::WordNetAdapter>("WordNet");
            if (wn) {
                int hits = 0;
                for (auto& t : bus.active_context().tokens) {
                    auto s = wn->lookup_synsets(t.word);
                    if (!s.empty()) { bus.active_context().synset_candidates[t.word] = s; ++hits; }
                }
                bus.log("pipeline", "L1 WN auto: " + std::to_string(hits) + " tokens have synsets");
            }
#endif

#ifdef MUJI_HAS_KIWIX
            auto kx = bus.get<muji::KiwixAdapter>("Kiwix");
            if (kx && kx->status() == muji::AdapterStatus::Ready) {
                int hits = 0;
                for (auto& t : bus.active_context().tokens) {
                    auto entries = kx->get_by_title(t.word);
                    for (auto& e : entries) {
                        muji::SemanticContext::ZimArticle za;
                        za.zim_name = e.zim_name;
                        za.title = e.title;
                        za.path = e.path;
                        // strip HTML
                        std::string stripped;
                        bool in_tag = false;
                        for (char c : e.content) {
                            if (c == '<') in_tag = true;
                            else if (c == '>') { in_tag = false; stripped += ' '; }
                            else if (!in_tag) stripped += c;
                        }
                        if (stripped.size() > 2000) stripped.resize(2000);
                        za.content_preview = stripped;
                        bus.active_context().zim_articles[t.word].push_back(za);
                        ++hits;
                    }
                }
                bus.log("pipeline", "L3 Kiwix auto: " + std::to_string(hits) + " ZIM articles found");
            }
#endif

            // L4: TraceableVector — auto-vectorize first token (glue contract test)
            auto vec = bus.get<muji::VectorAdapter>("Vector");
            if (vec && !bus.active_context().tokens.empty()) {
                auto key = bus.active_context().tokens[0].word;
                bus.active_context().vectors[key] = vec->vectorize(key, bus.active_context());
                bus.log("pipeline", "L4 Vector auto: vectorized token[0]=\"" + key + "\" (" +
                         std::to_string(bus.active_context().vectors[key].components.size()) + " dims)");
            }
        }

        show_pipeline_panel();

        // ═══ Layer 3D: 语义图谱 ═══
        show_3d_scene_panel();

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(window, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Native GL framebuffer screenshot
        const char* shot_path = std::getenv("MUJI_SCREENSHOT");
        { FILE* t = std::fopen("/tmp/muji_trigger_shot.txt", "w"); if (t) { std::fprintf(t, "one-shot dw=%d dh=%d env=%s\n", dw, dh, shot_path ? shot_path : "(null)"); std::fclose(t); } }
        if (shot_path && *shot_path) {
            glFinish();
            std::vector<unsigned char> pixels(dw * dh * 3);
            glReadPixels(0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::vector<unsigned char> flipped(dw * dh * 3);
            for (int y = 0; y < dh; ++y)
                std::memcpy(&flipped[y * dw * 3], &pixels[(dh - 1 - y) * dw * 3], dw * 3);
            FILE* f = std::fopen(shot_path, "wb");
            if (f) {
                std::fprintf(f, "P6\n%d %d\n255\n", dw, dh);
                std::fwrite(flipped.data(), 1, dw * dh * 3, f);
                std::fclose(f);
            }
        }
        glfwSwapBuffers(window);  // one final swap so window manager shows it briefly
    }

    muji::SemanticBus::instance().shutdown_all();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
