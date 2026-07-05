#include "btier/scoring_engine.h"

#include <algorithm>

namespace TOPNSPC::btier {

ScoringEngine::ScoringEngine(const BtierConfig &cfg)
    : cfg_(cfg), active_weights_(cfg.base_weights) {}

float ScoringEngine::score(uint64_t raw_metrics,
                           uint32_t last_access_time,
                           uint32_t current_time) const {
    std::shared_lock lock(weights_lock_);
    const WeightSet &w = active_weights_;

    // ── Normalize dimensions to [0, 1] ──

    // Recency: how recently was this extent accessed?
    // 1.0 = just now, 0.0 = older than cool_interval
    float norm_recency = 0.0f;
    if (cfg_.cool_interval_sec > 0) {
        uint32_t age = current_time - last_access_time;
        norm_recency = 1.0f - static_cast<float>(age) / static_cast<float>(cfg_.cool_interval_sec);
        norm_recency = std::clamp(norm_recency, 0.0f, 1.0f);
    }

    // Frequency: access count (12 bits, max 4095)
    uint32_t access_cnt = ExtentMetrics::access_count(raw_metrics);
    float norm_frequency =
        static_cast<float>(access_cnt) / 4095.0f;

    // Randomness: 6 bits, max 63
    // 0 = sequential, 63 = highly random
    uint32_t random = ExtentMetrics::randomness(raw_metrics);
    float norm_randomness = static_cast<float>(random) / 63.0f;

    // Write penalty: write count (12 bits, max 4095)
    uint32_t write_cnt = ExtentMetrics::write_count(raw_metrics);
    float norm_write_penalty =
        static_cast<float>(write_cnt) / 4095.0f;

    // ── 4D scoring formula ──
    // Score = w1 * recency + w2 * frequency + w3 * randomness
    //         - w4 * write_penalty
    float raw_score = w.w_recency * norm_recency + w.w_frequency * norm_frequency + w.w_randomness * norm_randomness - w.w_write_penalty * norm_write_penalty;

    // Clamp to [0, 1] — negative scores possible if write_penalty dominates
    return std::clamp(raw_score, 0.0f, 1.0f);
}

void ScoringEngine::adapt_weights(double fast_watermark) {
    WeightSet w = cfg_.base_weights;

    if (fast_watermark > cfg_.high_watermark) {
        // High pressure: amplify write penalty and randomness to
        // accelerate demotion of write-heavy / random extents
        float pressure = static_cast<float>(
            (fast_watermark - cfg_.high_watermark) /
            (1.0 - cfg_.high_watermark));
        w.w_write_penalty *= 1.0f + pressure * 2.0f;
        w.w_randomness *= 1.0f + pressure * 1.5f;
        w.w_recency *= 1.0f - pressure * 0.5f;
    } else if (fast_watermark < cfg_.low_watermark) {
        // Low pressure: boost recency and frequency to promote
        // recently-active extents to FAST
        w.w_recency *= 1.2f;
        w.w_frequency *= 1.1f;
    }

    std::unique_lock lock(weights_lock_);
    active_weights_ = w;
}

WeightSet ScoringEngine::current_weights() const {
    std::shared_lock lock(weights_lock_);
    return active_weights_;
}

}  // namespace TOPNSPC::btier
