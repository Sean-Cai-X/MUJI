// src/adapters/MeCabAdapter.cpp
// Glue-layer adapter for MeCab (Japanese morphological analyzer).
//
// Uses MeCab C API directly (mecab_new / mecab_parse_lattice).
// All MeCab specifics are isolated here — higher layers (SemanticBus,
// ImGui panels) only see muji::SemToken.

#include "MeCabAdapter.h"

#include <mecab.h>   // C API — from toolchain-rootfs/usr/include (via CMake find_path)
#include <cstring>
#include <cstdio>

namespace muji {

MeCabAdapter::MeCabAdapter() : AbstractAdapter("MeCab") {}

MeCabAdapter::~MeCabAdapter() {
    if (mecab_) shutdown();
}

bool MeCabAdapter::init() {
    if (mecab_) return true;

    // 构造 mecab_new(argc, argv) 参数
    // 格式: mecab_new(int argc, char **argv)
    std::vector<const char*> argv;
    std::vector<std::string> storage;  // 持有 std::string 直到 mecab_new 返回

    storage.push_back("MeCabAdapter");   // argv[0] — 程序名占位
    argv.push_back(storage.back().c_str());

    if (!config_.dicdir.empty()) {
        storage.push_back("-d");
        storage.push_back(config_.dicdir);
        argv.push_back("-d");
        argv.push_back(storage.back().c_str());
    }
    if (!config_.rcfile.empty()) {
        storage.push_back("-r");
        storage.push_back(config_.rcfile);
        argv.push_back("-r");
        argv.push_back(storage.back().c_str());
    }
    if (!config_.userdic.empty()) {
        storage.push_back("-u");
        storage.push_back(config_.userdic);
        argv.push_back("-u");
        argv.push_back(storage.back().c_str());
    }
    if (config_.partial) {
        storage.push_back("-p");
        argv.push_back("-p");
    }
    if (config_.marginal) {
        storage.push_back("-m");
        argv.push_back("-m");
    }
    if (config_.nb1est > 1) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "-N%d", config_.nb1est);
        storage.push_back(buf);
        argv.push_back(storage.back().c_str());
    }

    // mecab_new 返回 mecab_t*，失败返回 NULL
    // 注意: MeCab 会复制 argv 里的字符串，storage 可以在 mecab_new 返回后释放
    mecab_ = mecab_new(static_cast<int>(argv.size()),
                       const_cast<char**>(argv.data()));

    if (!mecab_) {
        set_last_error(std::string("mecab_new failed (no dicdir?): ") +
                       mecab_strerror(nullptr));
        return false;
    }

    set_status(AdapterStatus::Ready);
    return true;
}

bool MeCabAdapter::shutdown() {
    if (mecab_) {
        mecab_destroy(mecab_);
        mecab_ = nullptr;
    }
    set_status(AdapterStatus::Shutdown);
    return true;
}

bool MeCabAdapter::reload() {
    // Must call shutdown() to mecab_destroy() the native handle BEFORE nulling it.
    // Previously this did `mecab_ = nullptr;` directly, leaking the mecab_t*.
    shutdown();
    return init();
}

bool MeCabAdapter::segment(const std::string& text,
                           std::vector<SemToken>& out) const {
    if (!mecab_) {
        // Adapter 必须先 init()
        return false;
    }
    out.clear();
    if (text.empty()) return true;

    // mecab_sparse_tonode2: 最简单的高层 API
    // - 直接传 text + len, 返回 BOS 节点 (mecab_node_t*)
    // - 节点链表通过 node->next 遍历, 直到 NULL
    // - BOS/EOS 节点需要跳过 (stat == MECAB_BOS_NODE / MECAB_EOS_NODE)
    // - 内部内存由 MeCab tagger 持有, mecab_destroy 时统一释放
    const mecab_node_t* node = mecab_sparse_tonode2(
        const_cast<mecab_t*>(mecab_),
        text.c_str(),
        text.size());

    if (!node) {
        // 词典路径错等 — mecab_strerror 拿错误
        const char* err = mecab_strerror(const_cast<mecab_t*>(mecab_));
        set_last_error(std::string("mecab_sparse_tonode2 failed: ") +
                       (err ? err : "unknown (dicdir empty?)"));
        return false;
    }

    int32_t cursor = 0;

    while (node) {
        // BOS/EOS 节点跳过
        if (node->stat == MECAB_BOS_NODE || node->stat == MECAB_EOS_NODE) {
            node = node->next;
            continue;
        }

        SemToken t;
        t.word = std::string(node->surface, node->length);  // 表层词 (UTF-8 bytes)
        t.start = cursor;
        t.end   = cursor + static_cast<int32_t>(node->length);
        t.source = SourceTag::MeCabMorpheme;
        t.is_oov = (node->stat == MECAB_UNK_NODE);

        // feature 格式 (默认): POS, 子POS, 品詞細分, 活用型, 活用形, 原形, 読み, 発音
        //      例如: "名詞,一般,*,*,*,*,東京,トウキョウ,トウキョウ"
        if (node->feature) {
            // 按逗号切分 feature — 最多取前 16 个字段
            // mecab_node_t::feature 是 const char*, 我们复制一份来切分
            const size_t flen = strlen(node->feature);
            std::string fcopy(node->feature, flen);
            char* buf = &fcopy[0];

            char* fields[16] = {nullptr};
            int nf = 0;
            char* p = buf;
            fields[nf++] = p;
            while ((p = strchr(p, ',')) && nf < 16) {
                *p++ = '\0';
                fields[nf++] = p;
            }

            if (nf >= 1) t.pos    = fields[0];   // 品詞 e.g. "名詞"
            if (nf >= 7) t.lemma  = fields[6];   // 原形 e.g. "東京"
            if (nf >= 8) t.reading = fields[7];  // 読み e.g. "トウキョウ"
        }

        cursor = t.end;
        out.push_back(std::move(t));

        node = node->next;
    }

    return true;
}

} // namespace muji
