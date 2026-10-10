#include "adapters/VectorAdapter.h"
#include <algorithm>
#include <cctype>

namespace muji {

static std::string to_lower(std::string s) {
    for (auto& c : s) c = std::tolower((unsigned char)c);
    return s;
}

static std::string strip_html(std::string s) {
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (char c : s) {
        if (c == '<') in_tag = true;
        else if (c == '>') { in_tag = false; out += ' '; }
        else if (!in_tag) out += c;
    }
    return out;
}

TraceableVector VectorAdapter::vectorize(const std::string& key,
                                          const SemanticContext& ctx) const {
    TraceableVector v;

    // 分量 1: WordNet synset count (↑=更具体)
    int n_synsets = 0;
    auto sit = ctx.synset_candidates.find(key);
    if (sit != ctx.synset_candidates.end()) n_synsets = (int)sit->second.size();
    v.components.push_back(n_synsets / 10.0f);
    v.dim_labels.push_back("wordnet:synset_count");
    v.dim_sources.push_back(SourceTag::WordNetSynset);

    // 分量 2: 来自字典的 definition 数量
    int n_defs = 0;
    auto dit = ctx.definitions.find(key);
    if (dit != ctx.definitions.end()) n_defs = (int)dit->second.size();
    v.components.push_back(n_defs / 20.0f);
    v.dim_labels.push_back("dict:definition_count");
    v.dim_sources.push_back(SourceTag::GoldenDict);

    // 分量 3: 总定义文本长度
    size_t def_len = 0;
    if (dit != ctx.definitions.end()) {
        for (auto& e : dit->second) def_len += e.definition.size();
    }
    v.components.push_back(std::min(def_len / 200.0f, 1.0f));
    v.dim_labels.push_back("dict:total_chars_normalized");
    v.dim_sources.push_back(SourceTag::StarDict);

    // 分量 4: ZIM article count
    int n_zim = 0;
    auto zit = ctx.zim_articles.find(key);
    if (zit != ctx.zim_articles.end()) n_zim = (int)zit->second.size();
    v.components.push_back(n_zim / 5.0f);
    v.dim_labels.push_back("zim:article_count");
    v.dim_sources.push_back(SourceTag::KiwixZIM);

    return v;
}

TraceableSimilarity VectorAdapter::similarity(const std::string& a, const std::string& b,
                                               const SemanticContext& ctx) const {
    TraceableSimilarity sim;
    auto va = vectorize(a, ctx);
    auto vb = vectorize(b, ctx);

    // Dot product with per-dim weight (weights_ 由 UI 滑块校准)
    float dot = 0;
    for (size_t i = 0; i < std::min(va.components.size(), vb.components.size()); ++i) {
        float w = 1.0f;
        auto s = va.dim_sources[i];
        switch (s) {
            case SourceTag::WordNetSynset: w = weights_.wordnet_axiom_weight; break;
            case SourceTag::GoldenDict:
            case SourceTag::StarDict:      w = weights_.dict_definition_weight; break;
            case SourceTag::KiwixZIM:      w = weights_.zim_entity_weight; break;
            default:                        w = weights_.corpus_weight; break;
        }
        float contrib = va.components[i] * vb.components[i] * w;
        dot += contrib;
        sim.breakdown.push_back({va.dim_labels[i], contrib});
    }
    sim.total = std::clamp(dot, 0.0f, 1.0f);
    return sim;
}

} // namespace muji
