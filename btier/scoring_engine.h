#pragma once

#include <cstdint>
#include <shared_mutex>

#include "btier/btier_types.h"
#include "btier/config.h"
#include "common/common_fwd.h"

namespace TOPNSPC::btier {

// ── ScoringEngine ───────────────────────────────────────────────
// Computes a composite "heat score" for each extent.
//
// Deep module: callers only see score() and adapt_weights().
// The formula, normalization, and adaptation policy are hidden.
//
// Thread safety:
//   - active_weights_ protected by shared_mutex
//   - adapt_weights() takes exclusive lock (rare: once per scan cycle)
//   - score() takes shared lock (frequent but uncontended — single
//     background thread)
//
// Lifetime: Caller must ensure cfg outlives this instance (cfg_ is a
// reference, not a copy).
class ScoringEngine {
public:
    explicit ScoringEngine(const BtierConfig &cfg);

    // Returns a value in [0, 1]. Higher = hotter (more likely to stay
    // on FAST or be promoted). Lower = colder (candidate for demotion).
    // The absolute value is not meaningful across time; relative ordering
    // among extents determines migration priority.
    //
    // raw_metrics: packed ExtentMetrics word (access_count, write_count,
    //              randomness, state, generation)
    // last_access_time: seconds since epoch of last I/O
    // current_time: current time in seconds since epoch
    float score(uint64_t raw_metrics,
                uint32_t last_access_time,
                uint32_t current_time) const;

    // Adjusts internal weights based on FAST tier utilization.
    // Call this before each scoring pass.
    void adapt_weights(double fast_watermark);

    WeightSet current_weights() const;

private:
    const BtierConfig &cfg_;
    mutable std::shared_mutex weights_lock_;
    WeightSet active_weights_;
};

}  // namespace TOPNSPC::btier
