-- src/schema/japanese_wordnet.sql
-- Japanese WordNet (wn-ja) schema: 导入 + 和英文 WordNet 做 synset 对齐
--
-- 数据源: https://github.com/bond-lab/wnja  (BSD 许可)
-- 导入文件格式: LMF/XML 或 TSV
--   wn-ja 提供 3 个核心 TSV:
--     wn-ja.synset.tsv     → jwn_synset
--     wn-ja.lemma.tsv      → jwn_lemma (一个 synset 对应多个 lemma)
--     wn-ja.relation.tsv   → jwn_relation
--     wn-ja.def.tsv        → jwn_definition
--     wn-ja.englink.tsv    → jwn_eng_link (预计算好的日-英 synset 链接)
--
-- 目标: 把日文 synset 和英文 WordNet 3.0 synset 通过以下两种方式对齐:
--   1. wn-ja 自带的 englink (最权威, 直接 link 到 WN30 synset_id)
--   2. 无 englink 时通过 lemma 匹配 + 上下文相似度 (fallback)

PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

-- ═══════════════════════════════════════════════════════════════
-- 1. Japanese WordNet 核心表 (与英文 WordNet 同构, 但日文专用)
-- ═══════════════════════════════════════════════════════════════

-- 日文 synset (义项节点)
CREATE TABLE IF NOT EXISTS jwn_synset (
    jwn_synset_id   TEXT PRIMARY KEY,          -- e.g. "00000002-v" (与 WN 同格式)
    pos             TEXT NOT NULL,              -- 'n' 'v' 'a' 'r' 'u' 'x' (u=感動詞, x=記号)
    definition_ja   TEXT,                      -- 日文释义 (日本語による定義)
    definition_en   TEXT,                      -- 英文释义 (wn-ja 同时提供)
    -- cross-lingual alignment ↓
    eng_synset_id   TEXT,                      -- 对应英文 WordNet 3.0 synset e.g. "00000174-n"
    align_source    TEXT DEFAULT 'englink',    -- 'englink'(官方), 'lemma_match', 'context_sim', 'manual'
    align_conf      REAL DEFAULT 1.0,          -- 对齐置信度 0~1, 官方 englink=1.0
    created_at      INTEGER DEFAULT (unixepoch())
);

-- 日文 lemma (每个 synset 多个 lemma, 允许汉字 / 假名 / 混合)
CREATE TABLE IF NOT EXISTS jwn_lemma (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    jwn_synset_id   TEXT NOT NULL REFERENCES jwn_synset(jwn_synset_id) ON DELETE CASCADE,
    lemma           TEXT NOT NULL,              -- 原形 e.g. "情報処理", "じょうほうしょり"
    reading         TEXT,                       -- 读音 (ひらがな) e.g. "じょうほうしょり"
    lemma_lang      TEXT DEFAULT 'ja',          -- 'ja' (日文) / 'en' (英文 lemma 混入)
    is_primary      INTEGER DEFAULT 0,          -- 1 = 主要表記, 0 = 别名/表記揺れ
    UNIQUE(jwn_synset_id, lemma)
);

-- 日文 synset 之间的关系 (与 WordNetRelType 对齐, 但日文独有关系也可能出现)
CREATE TABLE IF NOT EXISTS jwn_relation (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    from_synset     TEXT NOT NULL REFERENCES jwn_synset(jwn_synset_id) ON DELETE CASCADE,
    to_synset       TEXT NOT NULL REFERENCES jwn_synset(jwn_synset_id) ON DELETE CASCADE,
    rel_type        TEXT NOT NULL,              -- 'hypernym' 'hyponym' 'synonym' 'antonym' 'meronym' ...
    rel_source      TEXT DEFAULT 'jwn',         -- 'jwn'(wn-ja 原生), 'en_projected'(从英文 WN 投影过来), 'manual'
    FOREIGN KEY (from_synset) REFERENCES jwn_synset(jwn_synset_id),
    FOREIGN KEY (to_synset)   REFERENCES jwn_synset(jwn_synset_id)
);

-- 日文词义定义 (多语言/多来源)
CREATE TABLE IF NOT EXISTS jwn_definition (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    jwn_synset_id   TEXT NOT NULL REFERENCES jwn_synset(jwn_synset_id) ON DELETE CASCADE,
    lang            TEXT NOT NULL,              -- 'ja' 'en' 'zh' ...
    definition      TEXT NOT NULL,
    source          TEXT DEFAULT 'wn-ja'
);

-- ═══════════════════════════════════════════════════════════════
-- 2. 跨语言对齐索引 (核心 — 连接 WN-EN ↔ WN-JA)
-- ═══════════════════════════════════════════════════════════════
--
-- 关键: jwn_synset.eng_synset_id 已经做了最直接的日→英映射,
-- 但仍需要一个专门的 cross_lingual_synset 表来做多跳查询和 fallback。

CREATE TABLE IF NOT EXISTS cross_lingual_synset (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    eng_synset_id   TEXT NOT NULL,              -- 英文 WordNet 3.0 synset e.g. "00000174-n"
    jwn_synset_id   TEXT NOT NULL,              -- 对应日文 synset e.g. "00000002-v"
    alignment_type  TEXT NOT NULL,              -- 'direct' (englink 官方), 'lemma_fallback', 'context_fallback', 'manual'
    confidence      REAL NOT NULL,              -- 0.0~1.0, 官方=1.0, fallback=0.5~0.8
    notes           TEXT,                       -- 备注 e.g. "lemmas matched: memory=記憶"
    created_at      INTEGER DEFAULT (unixepoch()),
    UNIQUE(eng_synset_id, jwn_synset_id)
);

-- GermaNet 也通过同一个 cross_lingual_synset 表对齐, 用 'de' 前缀区分
-- (但 GermaNet 有 ILI 索引, 见 germanet_ili.sql — 可以把 ILI 也加进来)
ALTER TABLE cross_lingual_synset ADD COLUMN de_synset_id TEXT;  -- GermaNet synset
ALTER TABLE cross_lingual_synset ADD COLUMN ili_number  TEXT;   -- ILI 跨语言编号 (全局唯一)

-- ═══════════════════════════════════════════════════════════════
-- 3. 索引 (加速 MeCab lemma → jwn_lemma → jwn_synset → eng_synset 链路)
-- ═══════════════════════════════════════════════════════════════

CREATE INDEX IF NOT EXISTS idx_jwn_lemma_lemma   ON jwn_lemma(lemma);
CREATE INDEX IF NOT EXISTS idx_jwn_lemma_synset ON jwn_lemma(jwn_synset_id);
CREATE INDEX IF NOT EXISTS idx_jwn_synset_eng   ON jwn_synset(eng_synset_id);
CREATE INDEX IF NOT EXISTS idx_jwn_rel_from     ON jwn_relation(from_synset);
CREATE INDEX IF NOT EXISTS idx_jwn_rel_to       ON jwn_relation(to_synset);
CREATE INDEX IF NOT EXISTS idx_cls_eng          ON cross_lingual_synset(eng_synset_id);
CREATE INDEX IF NOT EXISTS idx_cls_jwn          ON cross_lingual_synset(jwn_synset_id);
CREATE INDEX IF NOT EXISTS idx_cls_de           ON cross_lingual_synset(de_synset_id);
CREATE INDEX IF NOT EXISTS idx_cls_ili          ON cross_lingual_synset(ili_number);

-- ═══════════════════════════════════════════════════════════════
-- 4. 典型查询 (MeCab 输出 lemma → 英文 WordNet synset)
-- ═══════════════════════════════════════════════════════════════
--
-- -- MeCab lemma "記憶" → 英文 WordNet synset:
-- SELECT j.jwn_synset_id, j.eng_synset_id, j.pos, j.align_conf
-- FROM jwn_lemma l
-- JOIN jwn_synset j ON l.jwn_synset_id = j.jwn_synset_id
-- WHERE l.lemma = '記憶' AND j.eng_synset_id IS NOT NULL;
--
-- -- 反向: 英文 synset "03790840-n" (memory) → 日文候选:
-- SELECT j.jwn_synset_id, j.pos, l.lemma
-- FROM jwn_synset j
-- JOIN jwn_lemma l ON l.jwn_synset_id = j.jwn_synset_id
-- WHERE j.eng_synset_id = '03790840-n';
--
-- -- 通过 cross_lingual_synset 查 GermaNet → JWN 多跳:
-- SELECT jwn.jwn_synset_id, gn.de_synset_id
-- FROM cross_lingual_synset gn2jwn
-- WHERE gn2jwn.eng_synset_id = '03790840-n';
