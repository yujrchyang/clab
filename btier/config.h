#pragma once

#include <cstdint>
#include <string>

#include "common/common_fwd.h"

namespace TOPNSPC::btier {

// ── WeightSet (single definition) ───────────────────────────────
// Used by ScoringEngine and BtierConfig.
// Default base weights — tuned for mixed workload.
struct WeightSet {
    float w_recency = 0.35f;
    float w_frequency = 0.30f;
    float w_randomness = 0.25f;
    float w_write_penalty = 0.10f;
};

struct BtierConfig {
    // ── Device paths (init-only) ─────────────────────────────────
    std::string fast_dev_path;
    std::string slow_dev_path;

    // ── Extent geometry (init-only) ──────────────────────────────
    uint64_t extent_size = 4 * 1024 * 1024;
    uint64_t block_size = 4096;
    uint64_t large_value_threshold = 2 * 1024 * 1024;

    // ── Scoring (runtime-mutable via set_weights) ────────────────
    WeightSet base_weights;

    // ── Watermarks (runtime-mutable via set_watermarks) ──────────
    double low_watermark = 0.30;
    double high_watermark = 0.80;

    // ── Migration (scan_interval is runtime-mutable) ─────────────
    uint32_t scan_interval_ms = 1000;
    uint32_t max_migrations_per_cycle = 16;
    uint32_t max_compactions_per_cycle = 4;

    // ── Tier migration thresholds (runtime-mutable) ───────────────
    float promote_threshold = 0.7f;
    float demote_threshold = 0.3f;

    // ── Compaction (init-only) ───────────────────────────────────
    double compaction_dead_ratio = 0.50;
    double compaction_usage_ratio = 0.80;

    // ── Cooling (init-only) ──────────────────────────────────────
    uint32_t cool_interval_sec = 300;

    // ── Stride thresholds (init-only) ────────────────────────────
    uint64_t sequential_threshold = 64 * 1024;

    // ── Journal size (init-only) ──────────────────────────────────
    uint64_t journal_size = 64 * 1024 * 1024;  // 64MB default

    // ── File-based config loading ──────────────────────────────────
    static BtierConfig load(const std::string &path);
    int save(const std::string &path) const;
};

}  // namespace TOPNSPC::btier
