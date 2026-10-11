// src/data/CrossLingualSynset.h
// Japanese WordNet (wn-ja) + English WordNet (wn-en) + 中文词网 (wn-zh)
// 跨语言 synset 对齐 schema — header-only, 零依赖, 可直接嵌入 SemanticTypes.h
//
// 定位: MeCab 日文形态素 → lemma → wn-ja synset → 跨语言映射 → wn-en / wn-zh synset
//       这样下游 WordNetAdapter / GoldenDict 可以统一查询中英日三语
//
// 设计原则:
//   - 和现有 SemanticTypes.h 里的 SynsetNode / WordNetRelation 零耦合 (独立 struct)
//   - 每个字段有明确溯源标签 (SourceTag 复用现有枚举)
//   - 三语对齐关系是显式定义的, 不是统计推断 — 符合母机 "可溯源" 核心要求
//   - 映射表可以是本地 JSON/SQLite, 也可以是硬编码 bootstrap 条目
//
// 关联的上游:
//   - wn-ja (Japanese WordNet, 国立国语研究所): https://github.com/jordigh/wn-ja
//   - wn-en (WordNet 3.0): /usr/share/wordnet (system) 或 toolchain-rootfs
//   - wn-zh (中文词网, 哈工大): 可选, 作为 GoldenDict 词典库接入
//
// 典型用法 (在 MeCabAdapter segment 之后):
//   1. MeCab 输出 SemToken{word="情報処理", lemma="情報処理", source=MeCabMorpheme}
//   2. CrossLingualSynsetMapper::lookup_ja("情報処理") → SynsetNode{wn_ja_sid="00001234-n", ...}
//   3. CrossLingualSynsetMapper::align("00001234-n", "en") → wn-en synset_id
//   4. 下游 WordNetAdapter 拿到 wn-en synset_id, 查 hypernym / lemma

#pragma once

#include "SemanticTypes.h"   // SourceTag, SynsetNode, WordNetRelType
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <cstdint>

namespace muji {

// ── 语言标识 ─────────────────────────────────────────────────────
enum class LangTag : uint8_t {
    EN = 1,   // English WordNet
    JA = 2,   // Japanese WordNet (wn-ja)
    ZH = 3,   // 中文词网 (哈工大 pku-wict 或哈工大 HIT-WordNet)
    UNKNOWN = 0,
};

inline const char* to_cstr(LangTag l) {
    switch (l) {
        case LangTag::EN: return "en";
        case LangTag::JA: return "ja";
        case LangTag::ZH: return "zh";
        default:          return "?";
    }
}

// ── 跨语言 Synset 节点 ────────────────────────────────────────────
// 继承 SynsetNode 的语义, 扩展为三语版本
// 核心: 三个 lang × synset_id 的映射关系
struct CrossLingualSynset {
    std::string synset_id_en;     // e.g. "03790840-n" (WordNet 3.0)
    std::string synset_id_ja;     // e.g. "00001234-n" (wn-ja)
    std::string synset_id_zh;     // e.g. "00034567-n" (HIT-WordNet, 可选, 空=未对齐)

    // 品词 (三语一致, WordNetPos 枚举共享)
    WordNetPos  wn_pos = WordNetPos::Unknown;

    // 三语 lemmas — 同一 synset 概念的不同语言表达
    std::vector<std::string> lemmas_en;  // {"memory", "memorization"}
    std::vector<std::string> lemmas_ja;  // {"メモリ", "記憶", "メモリー"}
    std::vector<std::string> lemmas_zh;  // {"内存", "记忆", "存储器"}

    // 三语定义 (wordnet gloss 跨语言)
    std::string gloss_en;   // WordNet 英文定义
    std::string gloss_ja;   // wn-ja 日文定义
    std::string gloss_zh;   // 中文定义 (可选)

    // 对齐置信度 0.0~1.0 (1.0=手动确认, 0.7=自动+人工校验, 0.5=仅自动对齐)
    float align_confidence = 1.0f;

    // 来源 (对齐操作的溯源)
    SourceTag   source = SourceTag::WordNetSynset;

    // 备注: 对齐方法说明 (e.g. "手动确认 by 领域专家", "来自 Wikipedia interlang links")
    std::string align_method;
};

// ── 跨语言对齐关系 ────────────────────────────────────────────────
// 不只是 identity (同概念), 也可以是跨语言的部分/引申关系
// 例如: 中文 "内存" 对应英文 "memory" (上位), 但 MeCab 日文 "メモリ" 直接对应英文 "RAM"
// 这个结构用来表达: 同一语言内部的 synset 之间也有跨语言关系
struct CrossLingualRelation {
    std::string from_synset_en, from_synset_ja, from_synset_zh;
    std::string to_synset_en,   to_synset_ja,   to_synset_zh;

    // 对齐关系类型 — 扩展 WordNetRelType, 跨语言专用
    enum class CrossRelType : uint8_t {
        Identity  = 1,   // 完全同概念 (cross-lingual synonym)
        Hypernym  = 2,   // A 是 B 的上位 (跨语言)
        Hyponym   = 3,   // A 是 B 的下位 (跨语言)
        Meronym   = 4,   // A 是 B 的部分 (跨语言)
        Holonym   = 5,   // A 是 B 的整体 (跨语言)
        Approximate = 50, // 近似 (非精确对齐, 标注 align_confidence < 0.7)
        Loanword  = 60,  // 外来语 (e.g. 日文 "コンピュータ" ≈ 英文 "computer")
    } rel = CrossRelType::Identity;

    float confidence = 1.0f;
    SourceTag source = SourceTag::WordNetRel;
    std::string evidence;  // 对齐依据 (e.g. "Wikipedia interlang link: ja→en→zh")
};

// ── CrossLingualSynsetMapper (纯内存版, 可扩展到 SQLite) ─────────
// 单例或实例 — 负责: lemma → synset 查询, 跨语言对齐查询
// 接口设计为和 AbstractAdapter 解耦 — 上层通过 SemanticBus 访问
class CrossLingualSynsetMapper {
public:
    // 加载 wn-ja 编译好的词典 (wn-ja 是纯 text/TSV 格式, 直接读)
    bool load_wn_ja(const std::string& wn_ja_tsv_path);

    // 加载 wn-en 索引 (可选, 如果系统没有 WordNet C API 就用 wn-ja 自带的)
    bool load_wn_en_from_capi();  // 调 wn 命令行或直接用 C API

    // ── 查询 API ──────────────────────────────────────────────────────

    // 按 lemma + 语言 → 所有候选 synset (可能多义)
    std::vector<CrossLingualSynset> lookup(LangTag lang, const std::string& lemma) const;

    // 精确: synset_id + 语言 → CrossLingualSynset
    std::optional<CrossLingualSynset> get(LangTag lang, const std::string& synset_id) const;

    // 跨语言对齐: 给一个 synset_id + source_lang, 找 target_lang 的等价 synset
    std::optional<std::string> align(LangTag from_lang, const std::string& from_sid,
                                      LangTag to_lang) const;

    // 批量对齐: 给多语言 lemmas, 找出共同的 CrossLingualSynset
    std::vector<CrossLingualSynset> find_common(
        const std::vector<std::pair<LangTag, std::string>>& lemmas
    ) const;

    // 跨语言 WordNet 关系遍历 (复用 WordNetRelType)
    std::vector<CrossLingualRelation> get_relations(
        LangTag lang, const std::string& synset_id
    ) const;

    // ── 扩展: MeCab → CrossLingualSynset 桥接 ────────────────────────
    // 给定 MeCabAdapter 输出的 SemToken (有 lemma + reading),
    // 查 wn-ja synset, 然后对齐到 en/zh
    std::vector<CrossLingualSynset> resolve_mecab_token(
        const muji::SemToken& token
    ) const;

    // 给定 JiebaAdapter 输出的 SemToken (中文 lemma = word),
    // 查 wn-zh synset (如果有), 然后对齐到 en/ja
    std::vector<CrossLingualSynset> resolve_jieba_token(
        const muji::SemToken& token
    ) const;

private:
    // 按 (lang, lemma) 索引 — 快速查找候选 synset
    // 内部用 unordered_map<std::string, std::vector<CrossLingualSynset>>, key = "ja:情報処理"
    std::unordered_map<std::string, std::vector<CrossLingualSynset>> lemma_index_;
    std::unordered_map<std::string, CrossLingualSynset> synset_cache_;   // key = "ja:00001234-n"
    std::vector<CrossLingualRelation> relations_;
};

// ── SQL schema (参考) — 如果持久化到 SQLite 时用 ────────────────
// 实际项目可以用 SQLite/LevelDB 存, 也可以是纯 JSON 预加载
//
// CREATE TABLE synset_crosslang (
//   synset_id_ja    TEXT PRIMARY KEY,           -- wn-ja synset_id
//   synset_id_en    TEXT,                        -- WordNet 3.0 synset_id (可空)
//   synset_id_zh    TEXT,                        -- HIT-WordNet synset_id (可空)
//   wn_pos          INTEGER NOT NULL,            -- WordNetPos 枚举
//   lemmas_ja       TEXT NOT NULL,               -- JSON array: ["メモリ","記憶"]
//   lemmas_en       TEXT,                        -- JSON array
//   lemmas_zh       TEXT,                        -- JSON array
//   gloss_ja        TEXT,
//   gloss_en        TEXT,
//   gloss_zh        TEXT,
//   align_confidence REAL DEFAULT 1.0,
//   align_method    TEXT,                        -- "manual"|"wikipedia"|"auto_gpt"
//   source          INTEGER DEFAULT 10,          -- SourceTag::WordNetSynset
//   UNIQUE(synset_id_en),
//   UNIQUE(synset_id_zh)
// );
//
// CREATE TABLE synset_relations (
//   from_sid_ja   TEXT NOT NULL,
//   to_sid_ja     TEXT NOT NULL,
//   rel_type      INTEGER NOT NULL,   -- CrossRelType 枚举
//   confidence    REAL DEFAULT 1.0,
//   evidence      TEXT,
//   PRIMARY KEY(from_sid_ja, to_sid_ja, rel_type)
// );
//
// CREATE INDEX idx_synset_ja_lemma ON synset_crosslang(lemmas_ja);
// CREATE INDEX idx_synset_en_sid   ON synset_crosslang(synset_id_en);

} // namespace muji
