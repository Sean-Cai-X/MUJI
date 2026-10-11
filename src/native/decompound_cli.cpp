// src/native/decompound_cli.cpp
// 德语复合词拆分原生 CLI (German compound word decomposition)
//
// 原生独立入口: 可以单独编译、单独运行、单独更新
// 胶水层对接: UDPipeAdapter::split_compound() 可以 subprocess 调用此 CLI
//
// 策略: 启发式后缀列表 + 递归拆分 (未来可对接 GermaNet decompound 模块)
// 输入: argv[1] = 德语复合词, 如 "Speicherleistung"
// 输出: JSON 格式候选拆分路径 (可直接喂 ImGui 面板渲染)
//
// Usage:
//   decompound_cli Speicherleistung
//   decompound_cli --max 5 "Geschwindigkeitsbegrenzung"
//   echo "Speicherleistung" | decompound_cli -

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <iostream>
#include <sstream>

struct SubWord {
    std::string surface;
    std::string lemma;
    std::string pos;
    int         start = 0;
    int         end   = 0;
};

struct Candidate {
    std::vector<SubWord> parts;
    float confidence = 0.0f;
    int   source_tag = 0;  // 0=整体, 1=后缀启发式, 2=递归拆分
};

// ── 德语复合词常见后缀 (按置信度排序) ──
// 标注: N=Noun, V=Verb, A=Adjective, S=Suffix
struct Suffix { const char* suf; const char* pos; float weight; };
static const Suffix kSuffixes[] = {
    // 高置信度名词后缀 (N)
    {"station",  "NOUN", 0.90f},
    {"leitung",  "NOUN", 0.88f},
    {"leistung", "NOUN", 0.85f},
    {"begrenzung","NOUN", 0.82f},
    {"verbindung","NOUN", 0.80f},
    {"entwicklung","NOUN",0.80f},
    {"ausstattung","NOUN",0.78f},
    {"einrichtung","NOUN",0.78f},
    {"aufnahme", "NOUN", 0.75f},
    {"annahme",  "NOUN", 0.75f},
    {"wanderung","NOUN", 0.75f},
    // 中等置信度
    {"ung",      "NOUN", 0.65f},   // 动词→名词
    {"heit",     "NOUN", 0.70f},   // Adj→Noun
    {"keit",     "NOUN", 0.70f},   // Adj→Noun
    {"tion",     "NOUN", 0.72f},   // 外来词
    {"sion",     "NOUN", 0.72f},
    {"ität",     "NOUN", 0.70f},
    {"ist",      "NOUN", 0.65f},   // Person
    {"eur",      "NOUN", 0.60f},
    {"er",       "NOUN", 0.50f},   // 可能是复合词后缀也可能是派生词
    {"ung",      "NOUN", 0.65f},
    {"ung",      "NOUN", 0.65f},
    // 形容词后缀 (A)
    {"lich",     "ADJ",  0.55f},
    {"isch",     "ADJ",  0.55f},
    {"los",      "ADJ",  0.55f},
    {"voll",     "ADJ",  0.55f},
    // 动词后缀 (V)
    {"ieren",    "VERB", 0.50f},
    {"igen",     "VERB", 0.45f},
    {"ern",      "VERB", 0.40f},
    // 连接词素 (Fugenelemente, 德语复合词的 "粘合剂")
    {"s",        "LINK", 0.75f},   // Speicher-s-leistung
    {"n",        "LINK", 0.65f},
    {"en",       "LINK", 0.55f},
    {"er",       "LINK", 0.35f},
    {"e",        "LINK", 0.45f},
    {"o",        "LINK", 0.45f},
    {"i",        "LINK", 0.40f},
    {"a",        "LINK", 0.35f},
};
static const int kNumSuffixes = sizeof(kSuffixes) / sizeof(kSuffixes[0]);

// ── 生成候选拆分路径 ──
void decompound(const std::string& word, int max_candidates,
                std::vector<Candidate>& out) {
    if (word.empty()) return;
    const int n = (int)word.size();

    // 预排序后缀: 长度降序 → 更长后缀优先匹配 (避免 "leistung" 被 "ung" 抢先)
    std::vector<Suffix> sorted_suf(kSuffixes, kSuffixes + kNumSuffixes);
    std::sort(sorted_suf.begin(), sorted_suf.end(),
              [](const Suffix& a, const Suffix& b) {
                  return strlen(a.suf) > strlen(b.suf);
              });

    // Candidate 0: 整体不拆分
    {
        Candidate c;
        c.confidence = 0.5f;
        c.source_tag = 0;
        c.parts.push_back({word, word, "NOUN", 0, n});
        out.push_back(c);
    }

    // 遍历后缀, 找所有可能拆分点 (长后缀已优先)
    for (const auto& suf : sorted_suf) {
        int slen = (int)strlen(suf.suf);
        if (slen >= n) continue;

        // 后缀在中间 (复合词: 前 + 后缀 + 后) 或 末尾 (派生词: 词根 + 后缀)
        // 中间模式: pos < n-slen (后缀不是整个词的最后 slen 字符)
        // 末尾模式: pos == n-slen (后缀刚好在末尾, 派生词模式)
        for (int pos = 1; pos <= n - slen; ++pos) {
            if (word.compare(pos, slen, suf.suf) != 0) continue;

            bool is_end = (pos + slen == n);  // 后缀在末尾

            // 跳过: 后缀在末尾但太短 (至少要让词根 >= 3)
            if (is_end && pos < 3) continue;
            // 跳过: 后缀在中间但 rest 太短
            if (!is_end && n - pos - slen < 3) continue;
            // 跳过: 词根太短
            if (pos < 3) continue;

            std::string first = word.substr(0, pos + slen);
            std::string rest  = is_end ? "" : word.substr(pos + slen);

            Candidate c;
            c.source_tag = 1;
            float conf = suf.weight * (is_end ? 0.9f : 0.8f);
            c.parts.push_back({word.substr(0, pos), word.substr(0, pos), "NOUN", 0, pos});
            c.parts.push_back({suf.suf, suf.suf, suf.pos, pos, pos + slen});
            if (!is_end) {
                c.parts.push_back({rest, rest, "NOUN", pos + slen, n});
                conf *= 0.7f;  // 三拆分稍降权
            }
            c.confidence = conf;
            out.push_back(c);
        }
    }

    // ── Fugenelement 拆分 (德语复合词 "连接词素": s/n/en/e) ──
    // 模式: 词根 + 连接词素 + 词根 (如 Speicher-s-leistung, Geschwindigkeit-s-begrenzung)
    static const char* kLinkers[] = {"s", "n", "en", "e", "er", "o", "i"};
    static const int kLinkerLen[] = {1, 1, 2, 1, 2, 1, 1};
    const int nLinkers = sizeof(kLinkers) / sizeof(kLinkers[0]);
    for (int li = 0; li < nLinkers; ++li) {
        int llen = kLinkerLen[li];
        const char* lname = kLinkers[li];
        // 只在 s 等 1-char linker 时跳过太短 (s=1 而词根至少 3 才能再拼)
        for (int pos = 3; pos <= n - llen - 3; ++pos) {
            if (word.compare(pos, llen, lname) != 0) continue;
            // 验证 linker 后面还有词根
            std::string left  = word.substr(0, pos);
            std::string right = word.substr(pos + llen);
            if (left.size() < 3 || right.size() < 3) continue;

            Candidate c;
            c.source_tag = 3;  // 连接词素模式
            c.parts.push_back({left, left, "NOUN", 0, pos});
            c.parts.push_back({lname, lname, "LINK", pos, pos + llen});
            c.parts.push_back({right, right, "NOUN", pos + llen, n});
            // 置信度: s linker 最常见, 其他降权
            float w = (llen == 1 && lname[0] == 's') ? 0.75f : 0.55f;
            c.confidence = w;
            out.push_back(c);
        }
    }

    // 递归拆分: 对每个候选的前半部分再尝试拆分
    std::vector<Candidate> recurse;
    for (auto& cand : out) {
        if (cand.parts.size() != 2) continue;  // 只对 2-part 的再拆
        std::string first = cand.parts[0].surface;
        std::string rest  = cand.parts[1].surface;

        // 尝试再拆分 first
        for (int si = 0; si < kNumSuffixes; ++si) {
            const auto& suf = kSuffixes[si];
            int slen = (int)strlen(suf.suf);
            int flen = (int)first.size();
            if (slen >= flen) continue;

            for (int pos = 1; pos <= flen - slen && pos + slen < flen; ++pos) {
                if (first.compare(pos, slen, suf.suf) != 0) continue;

                std::string sub1 = first.substr(0, pos + slen);
                std::string sub2 = first.substr(pos + slen);
                if (sub1.size() < 3 || sub2.size() < 3) continue;

                Candidate c;
                c.source_tag = 2;
                c.parts.push_back({sub1, sub1, suf.pos, 0, (int)sub1.size()});
                c.parts.push_back({sub2, sub2, "NOUN", (int)sub1.size(), (int)first.size()});
                c.parts.push_back({rest, rest, cand.parts[1].pos, (int)first.size(), n});
                c.confidence = cand.confidence * suf.weight * 0.7f;
                recurse.push_back(c);
            }
        }
    }
    out.insert(out.end(), recurse.begin(), recurse.end());

    // 排序: 置信度降序, 去掉重复
    std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        return a.confidence > b.confidence;
    });

    // 去重 (parts sequence 相同算重复)
    std::vector<Candidate> deduped;
    for (auto& c : out) {
        bool dup = false;
        for (auto& d : deduped) {
            if (d.parts.size() != c.parts.size()) continue;
            bool same = true;
            for (size_t i = 0; i < c.parts.size(); ++i)
                if (d.parts[i].surface != c.parts[i].surface) { same = false; break; }
            if (same) { dup = true; break; }
        }
        if (!dup) deduped.push_back(c);
        if ((int)deduped.size() >= max_candidates) break;
    }
    out = deduped;
}

// ── JSON 输出 ──
void emit_json(const std::string& word, const std::vector<Candidate>& cands) {
    std::printf("{\n");
    std::printf("  \"word\": \"%s\",\n", word.c_str());
    std::printf("  \"n_candidates\": %zu,\n", cands.size());
    std::printf("  \"candidates\": [\n");
    for (size_t ci = 0; ci < cands.size(); ++ci) {
        const auto& c = cands[ci];
        std::printf("    {\n");
        std::printf("      \"confidence\": %.3f,\n", c.confidence);
        std::printf("      \"source\": %d,\n", c.source_tag);
        std::printf("      \"parts\": [\n");
        for (size_t pi = 0; pi < c.parts.size(); ++pi) {
            const auto& p = c.parts[pi];
            std::printf("        {\"surface\": \"%s\", \"lemma\": \"%s\", \"pos\": \"%s\", \"start\": %d, \"end\": %d}%s\n",
                        p.surface.c_str(), p.lemma.c_str(), p.pos.c_str(), p.start, p.end,
                        (pi + 1 < c.parts.size()) ? "," : "");
        }
        std::printf("      ]\n");
        std::printf("    }%s\n", (ci + 1 < cands.size()) ? "," : "");
    }
    std::printf("  ]\n");
    std::printf("}\n");
}

// ── 从 stdin 读词 (管道模式) ──
std::string read_stdin() {
    std::string line;
    std::getline(std::cin, line);
    // trim
    size_t a = line.find_first_not_of(" \t\r\n");
    size_t b = line.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return line.substr(a, b - a + 1);
}

int main(int argc, char* argv[]) {
    int max_candidates = 3;
    std::string word;

    // parse args
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--max" && i + 1 < argc) { max_candidates = std::atoi(argv[++i]); }
        else if (a == "-") { word = read_stdin(); }
        else if (a.size() > 0 && a[0] != '-') { word = a; }
    }

    if (word.empty()) {
        std::fprintf(stderr,
            "Usage: decompound_cli [--max N] <word>\n"
            "       echo \"word\" | decompound_cli -\n"
            "Decomposes German compound words using suffix heuristics.\n"
            "Output: JSON with candidate split paths.\n");
        return 1;
    }

    std::vector<Candidate> cands;
    decompound(word, max_candidates, cands);
    emit_json(word, cands);
    return 0;
}
