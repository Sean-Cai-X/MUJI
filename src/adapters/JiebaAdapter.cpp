// src/adapters/JiebaAdapter.cpp

#include "JiebaAdapter.h"

#include <cppjieba/Jieba.hpp>
#include <cppjieba/KeywordExtractor.hpp>

namespace muji {

JiebaAdapter::JiebaAdapter() : AbstractAdapter("cppjieba") {}

JiebaAdapter::~JiebaAdapter() {
    // destructor calls shutdown only if still ready
    if (jieba_) shutdown();
}

bool JiebaAdapter::init() {
    if (jieba_) return true; // already initialized

    try {
        // Resolve relative paths against CWD. In production this should
        // resolve relative to MUJI executable location. For now: CWD.
        jieba_ = std::make_unique<cppjieba::Jieba>(
            config_.dict_path,
            config_.hmm_path,
            config_.user_dict_path,
            config_.idf_path,
            config_.stop_words_path
        );
    } catch (const std::exception& e) {
        set_last_error(std::string("cppjieba init failed: ") + e.what());
        return false;
    }

    if (!jieba_) {
        set_last_error("cppjieba init failed: unknown error");
        return false;
    }

    set_status(AdapterStatus::Ready);
    return true;
}

bool JiebaAdapter::shutdown() {
    jieba_.reset();
    set_status(AdapterStatus::Shutdown);
    return true;
}

bool JiebaAdapter::reload() {
    jieba_.reset();
    set_status(AdapterStatus::Uninitialized);
    return init();
}

bool JiebaAdapter::load_user_dict(const std::string& path) {
    config_.user_dict_path = path;
    return reload();
}

bool JiebaAdapter::segment(const std::string& text,
                           std::vector<SemToken>& out) const {
    if (!jieba_) return false;
    out.clear();
    if (text.empty()) return true;

    switch (config_.mode) {
    case JiebaConfig::Exact: {
        std::vector<std::pair<std::string, std::string>> words;
        jieba_->Tag(text, words);
        out.reserve(words.size());
        int32_t cursor = 0;
        for (auto& w : words) {
            SemToken t;
            t.word = w.first;
            t.pos  = w.second;  // cppjieba gives POS string directly
            size_t found = text.find(w.first, cursor);
            if (found == std::string::npos) found = cursor;
            t.start = static_cast<int32_t>(found);
            t.end   = static_cast<int32_t>(found + w.first.size());
            t.source = SourceTag::JiebaToken;
            out.push_back(std::move(t));
            cursor = t.end;
        }
        break;
    }
    case JiebaConfig::Full: {
        std::vector<std::string> words;
        jieba_->Cut(text, words, true);  // true = HMM
        int32_t cursor = 0;
        for (auto& w : words) {
            SemToken t;
            t.word = w;
            t.pos = "?"; // full mode has no tag
            size_t found = text.find(w, cursor);
            if (found == std::string::npos) found = cursor;
            t.start = static_cast<int32_t>(found);
            t.end   = static_cast<int32_t>(found + w.size());
            t.source = SourceTag::JiebaToken;
            out.push_back(std::move(t));
            cursor = t.end;
        }
        break;
    }
    case JiebaConfig::Search: {
        std::vector<std::string> words;
        jieba_->CutForSearch(text, words);
        int32_t cursor = 0;
        for (auto& w : words) {
            SemToken t;
            t.word = w;
            t.pos = "?";
            size_t found = text.find(w, cursor);
            if (found == std::string::npos) found = cursor;
            t.start = static_cast<int32_t>(found);
            t.end   = static_cast<int32_t>(found + w.size());
            t.source = SourceTag::JiebaToken;
            out.push_back(std::move(t));
            cursor = t.end;
        }
        break;
    }
    }
    return true;
}

} // namespace muji
