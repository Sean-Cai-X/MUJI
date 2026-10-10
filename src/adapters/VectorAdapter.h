#pragma once
#include "glue/AbstractAdapter.h"
#include "data/SemanticTypes.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace muji {

/// Layer 4 — TraceableVector engine stub.
/// 第一版: 从 SemanticContext 的 definitions / synsets / entities
/// 构造可溯源向量。真实实现会用 Eigen 做线性代数 + 多源权重合成。
/// 这版是 glue-layer 契约验证 — 保证 Pipeline 能走到 L4。
class VectorAdapter : public AbstractAdapter {
public:
    VectorAdapter() : AbstractAdapter("Vector") {}
    bool init() override { set_status(AdapterStatus::Ready); return true; }
    bool shutdown() override { set_status(AdapterStatus::Shutdown); return true; }

    /// 从 SemanticContext 生成向量 (stub: 用定义长度+synset数构造可溯源分量)
    TraceableVector vectorize(const std::string& key, const SemanticContext& ctx) const;

    /// 计算两个词的可溯源相似度 (stub: 基于 definition overlap)
    TraceableSimilarity similarity(const std::string& a, const std::string& b,
                                   const SemanticContext& ctx) const;

    /// 获取/设置 每类来源的权重系数 (UI 滑块校准用)
    struct WeightConfig {
        float wordnet_axiom_weight = 0.4f;
        float dict_definition_weight = 0.3f;
        float zim_entity_weight = 0.2f;
        float corpus_weight = 0.1f;
    };
    const WeightConfig& weights() const { return weights_; }
    void set_weights(WeightConfig w) { weights_ = w; }

private:
    WeightConfig weights_;
};

} // namespace muji
