-- src/schema/germanet_ili.sql
-- ODeNet 1.4 (Open Deutsches WordNet) ILI 索引与英文 WordNet 做 synset 映射 schema
--
-- 数据源: ODeNet 1.4 (Hochschule Darmstadt) https://github.com/hdaSprachtechnologie/odenet
--   - WN-LMF 1.0 标准 XML 格式, CC BY-SA 4.0 开源, 无注册门槛
--   - 36,268 synsets, 120,107 lexical entries, 100% ILI 覆盖 (每个 synset 都有 ILI)
--   - ILI 是全局唯一的跨语言语义编号 (Inter-Lingual Index):
--     同一个 ILI → 同一概念 → 不同语言的 synset (EN / DE / JA ...)
--
-- 对比 GermaNet (图宾根大学, 需注册):
--   GermaNet 20.0 ≈ 15 万 synset, 但约 60% 有 ILI, 需机构邮箱注册
--   ODeNet 1.4 ≈ 3.6 万 synset, 100% ILI, 完全开源, 无门槛
--   两者 schema 兼容 (表名 germanet_* 保留, 数据源切换到 ODeNet)
--
-- 核心优势:
--   ILI 枢纽 → English WN (通过 OMW) ↔ ODeNet ↔ Japanese WordNet
--   这是 MUJI 语义母机 "跨语言刻度统一" 的核心基础设施

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- ═══════════════════════════════════════════════════════════════
-- 1. ILI (Inter-Lingual Index) 全局索引表
-- ═══════════════════════════════════════════════════════════════
--
-- ILI 编号格式: "ili-28882362-n" (ILI ID + POS)
-- 每个 ILI 对应一个全局概念, 可以被任意语言的 WordNet synset 引用

CREATE TABLE IF NOT EXISTS ili_index (
    ili_id          TEXT PRIMARY KEY,          -- e.g. "ili-28882362-n"
    pos             TEXT NOT NULL,              -- 'n' 'v' 'a' 'r'
    concept_label   TEXT,                      -- 概念标签 (英文) e.g. "computer_memory"
    definition_en   TEXT,                      -- 英文定义
    definition_de   TEXT,                      -- 德文定义 (如果有)
    definition_ja   TEXT,                      -- 日文定义 (如果有)
    ili_source      TEXT DEFAULT 'WN-ILI',     -- 来源: WN-ILI, GermaNet, JWN, manual
    created_at      INTEGER DEFAULT (unixepoch())
);

-- ═══════════════════════════════════════════════════════════════
-- 2. GermaNet 核心表
-- ═══════════════════════════════════════════════════════════════

-- 德语 synset
CREATE TABLE IF NOT EXISTS germanet_synset (
    de_synset_id    TEXT PRIMARY KEY,          -- e.g. "s38462-n" (GermaNet 自有编号)
    pos             TEXT NOT NULL,              -- 'n' 'v' 'a' 'r' 'b' 'i' (b=Präposition, i=Interjektion)
    definition_de   TEXT,                      -- 德文定义
    ili_id          TEXT,                      -- 对应的 ILI 编号 (可空, 部分 synset 无 ILI)
    eng_synset_id   TEXT,                      -- 直接对应的英文 WordNet synset (非 ILI 路线的直接映射)
    align_via       TEXT DEFAULT 'ili',        -- 'ili', 'direct', 'lemma_match', 'manual'
    align_conf      REAL DEFAULT 1.0,
    created_at      INTEGER DEFAULT (unixepoch()),
    FOREIGN KEY (ili_id) REFERENCES ili_index(ili_id) ON DELETE SET NULL
);

-- 德语 lemma (每个 synset 多个 lemma)
CREATE TABLE IF NOT EXISTS germanet_lemma (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    de_synset_id    TEXT NOT NULL REFERENCES germanet_synset(de_synset_id) ON DELETE CASCADE,
    lemma           TEXT NOT NULL,              -- 德语原形 (首字母大写!) e.g. "Speicher"
    -- German 特有字段 ↓
    variant         TEXT,                      -- 变体 e.g. "Speicherzentrum" (复合词变体)
    pos_refinement  TEXT,                      -- 词性细化 e.g. "Nominativ" "Plural"
    has_compound    INTEGER DEFAULT 0,         -- 1 = 该 lemma 自身是复合词
    UNIQUE(de_synset_id, lemma)
);

-- GermaNet 关系 (标准 WordNet 关系 + 复合词专用关系)
CREATE TABLE IF NOT EXISTS germanet_relation (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    from_synset     TEXT NOT NULL REFERENCES germanet_synset(de_synset_id) ON DELETE CASCADE,
    to_synset       TEXT NOT NULL REFERENCES germanet_synset(de_synset_id) ON DELETE CASCADE,
    rel_type        TEXT NOT NULL,              -- 见下方 GermaNet 关系类型枚举
    rel_source      TEXT DEFAULT 'germanet',    -- 'germanet'(原生), 'en_projected', 'manual'
    -- GermaNet 独有: 复合词语义关系细分
    compound_type   TEXT,                      -- 仅 rel_type='compound_*' 时有值
                                               -- 'locative' (位置), 'functional' (功能),
                                               -- 'material' (材料), 'purpose' (目的)
    FOREIGN KEY (from_synset) REFERENCES germanet_synset(de_synset_id),
    FOREIGN KEY (to_synset)   REFERENCES germanet_synset(de_synset_id)
);

-- ═══════════════════════════════════════════════════════════════
-- 3. GermaNet 关系类型 (扩展 WordNet 标准集)
-- ═══════════════════════════════════════════════════════════════
--
-- 标准 WordNet 关系: hypernym / hyponym / synonym / antonym / meronym / holonym / derivation / instance / member
--
-- GermaNet 复合词专用 (关键 — 德语复合词的词法公理):
--   compound_meronym   : 复合词的部分 (Kompositum 拆分)
--   compound_holonym   : 复合词的整体
--   location_meronym   : 位置关系 (如 "Autotür" → Tür 是 Auto 的一部分, 位置=车身)
--   material_meronym   : 材料关系 (如 "Stahltür" → Tür 的材料是 Stahl)
--   purpose_meronym    : 功能关系 (如 "Schreibtischlampe" → Lampe 用于 Schreibtisch)
--   functional_meronym : 功能组成 (如 "Speicherkarte" → Karte 承担 Speicher 功能)
--
-- 德语特有:
--   pertainym           : 属有关系 (如 "wirtschaftlich" → 属于 Wirtschaft)
--   cause               : 因果关系

-- GermaNet 复合词拆分词典 (decompound 模块输出 → GermaNet synset 对齐)
CREATE TABLE IF NOT EXISTS germanet_compound_split (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    original_word   TEXT NOT NULL,              -- 原表层词 e.g. "Handyaufladestation"
    de_synset_id    TEXT,                      -- 整体词对应的 GermaNet synset (可空, 可能找不到)
    split_order     INTEGER NOT NULL,           -- 候选路径序号 (0 = best)
    confidence      REAL NOT NULL,              -- 0.0~1.0
    -- 一条路径 = 多个 part
    parts_json      TEXT NOT NULL,              -- JSON: [{"surface":"Handy","lemma":"Handy","pos":"NOUN"}, ...]
    source          TEXT DEFAULT 'decompound',  -- 'decompound', 'germanet_native', 'manual'
    FOREIGN KEY (de_synset_id) REFERENCES germanet_synset(de_synset_id) ON DELETE SET NULL
);

-- ═══════════════════════════════════════════════════════════════
-- 4. ILI 跨语言对齐 (核心枢纽表)
-- ═══════════════════════════════════════════════════════════════
--
-- 这张表把 GermaNet / Japanese WordNet / 英文 WordNet 三套 synset
-- 通过 ILI 编号统一锚定到同一个全局概念上。
--
-- 连接关系:
--   ili_index.ili_id  ← germanet_synset.ili_id
--                      ← english_wn (via cross_lingual_synset.ili_number)
--                      ← jwn_synset (也可以有 ili_id, 通过 cross_lingual_synset.ili_number)

-- 英文 WordNet ↔ ILI 对齐 (复用 japanese_wordnet.sql 里的 cross_lingual_synset)
-- 这里只需要确认 cross_lingual_synset.ili_number 字段存在即可 (已在 jwn schema 中 ALTER 添加)

-- GermaNet 也可通过 cross_lingual_synset.de_synset_id 对齐 (已在 jwn schema 中添加)
-- 但 GermaNet ↔ GermaNet 原生 ILI 关系单独维护更清晰:

CREATE TABLE IF NOT EXISTS ili_germanet_mapping (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    ili_id          TEXT NOT NULL REFERENCES ili_index(ili_id) ON DELETE CASCADE,
    de_synset_id    TEXT NOT NULL REFERENCES germanet_synset(de_synset_id) ON DELETE CASCADE,
    mapping_confidence REAL DEFAULT 1.0,
    mapping_source  TEXT DEFAULT 'germanet_native',  -- 'germanet_native', 'lemma_match', 'manual'
    UNIQUE(ili_id, de_synset_id)
);

-- ═══════════════════════════════════════════════════════════════
-- 5. 索引 (加速 UDPipe lemma → German synset → ILI → English/Japanese 链路)
-- ═══════════════════════════════════════════════════════════════

CREATE INDEX IF NOT EXISTS idx_ili_pos          ON ili_index(pos);
CREATE INDEX IF NOT EXISTS idx_gernet_lemma    ON germanet_lemma(lemma);
CREATE INDEX IF NOT EXISTS idx_gernet_syn_ili  ON germanet_synset(ili_id);
CREATE INDEX IF NOT EXISTS idx_gernet_syn_eng  ON germanet_synset(eng_synset_id);
CREATE INDEX IF NOT EXISTS idx_gernet_rel_from ON germanet_relation(from_synset);
CREATE INDEX IF NOT EXISTS idx_gernet_rel_to   ON germanet_relation(to_synset);
CREATE INDEX IF NOT EXISTS idx_gernet_compound ON germanet_compound_split(original_word);
CREATE INDEX IF NOT EXISTS idx_ili_gn_map       ON ili_germanet_mapping(ili_id);
CREATE INDEX IF NOT EXISTS idx_ili_gn_map_de   ON ili_germanet_mapping(de_synset_id);

-- ═══════════════════════════════════════════════════════════════
-- 6. 典型查询 (UDPipe 输出 lemma → GermaNet → 英文/日文 synset)
-- ═══════════════════════════════════════════════════════════════
--
-- -- UDPipe lemma "Speicher" (memory) → 英文 WordNet synset + Japanese synset:
-- SELECT
--     gs.de_synset_id,
--     gs.ili_id,
--     gs.eng_synset_id,
--     cls.jwn_synset_id
-- FROM germanet_lemma gl
-- JOIN germanet_synset gs ON gl.de_synset_id = gs.de_synset_id
-- LEFT JOIN cross_lingual_synset cls ON cls.ili_number = gs.ili_id
-- WHERE gl.lemma = 'Speicher';
--
-- -- GermaNet 复合词拆分子词 → GermaNet synset → English/WN-JA:
-- SELECT gc.original_word, gc.parts_json,
--        gs.de_synset_id, gs.ili_id, gs.eng_synset_id
-- FROM germanet_compound_split gc
-- LEFT JOIN germanet_synset gs ON gs.de_synset_id = gc.de_synset_id
-- WHERE gc.original_word = 'Handyaufladestation';
--
-- -- 反向: 英文 synset "03790840-n" (memory) → GermaNet 候选:
-- SELECT gs.de_synset_id, gs.pos, gl.lemma
-- FROM germanet_synset gs
-- JOIN germanet_lemma gl ON gl.de_synset_id = gs.de_synset_id
-- WHERE gs.eng_synset_id = '03790840-n';
--
-- -- ILI 聚合查询: 同一个概念的所有语言 synset:
-- SELECT
--     ili.ili_id,
--     gs.de_synset_id, gl.lemma AS de_lemma,
--     jss.jwn_synset_id, jl.lemma AS ja_lemma,
--     cls.eng_synset_id
-- FROM ili_index ili
-- LEFT JOIN ili_germanet_mapping igm ON igm.ili_id = ili.ili_id
-- LEFT JOIN germanet_synset gs ON gs.de_synset_id = igm.de_synset_id
-- LEFT JOIN germanet_lemma gl ON gl.de_synset_id = gs.de_synset_id AND gl.is_primary = 1
-- LEFT JOIN cross_lingual_synset cls ON cls.ili_number = ili.ili_id
-- LEFT JOIN jwn_synset jss ON jss.jwn_synset_id = cls.jwn_synset_id
-- LEFT JOIN jwn_lemma jl ON jl.jwn_synset_id = jss.jwn_synset_id AND jl.is_primary = 1
-- WHERE ili.concept_label LIKE '%memory%' OR gs.eng_synset_id = '03790840-n';
