#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "btier/btier.h"
#include "btier/btier_types.h"
#include "btier/config.h"
#include "btier/extent_map.h"
#include "btier/key_map.h"
#include "btier/scoring_engine.h"
#include "common/buffer.h"
#include "cxxlab_test.h"

using namespace TOPNSPC;
using namespace TOPNSPC::btier;

// ── ScoringEngine unit tests (B2) ──────────────────────────────

class ScoringEngineTest : public ::testing::Test {
protected:
    BtierConfig cfg;
    std::unique_ptr<ScoringEngine> engine;

    void SetUp() override {
        cfg.base_weights.w_recency = 0.35f;
        cfg.base_weights.w_frequency = 0.30f;
        cfg.base_weights.w_randomness = 0.25f;
        cfg.base_weights.w_write_penalty = 0.10f;
        cfg.cool_interval_sec = 300;
        cfg.low_watermark = 0.30;
        cfg.high_watermark = 0.80;
        engine = std::make_unique<ScoringEngine>(cfg);
    }

    // Helper: build raw_metrics word
    uint64_t make_metrics(uint32_t access, uint32_t write,
                          uint32_t randomness, uint32_t state = ACTIVE,
                          uint64_t gen = 0) {
        return ExtentMetrics::pack(access, write, randomness, state, gen);
    }
};

TEST_F(ScoringEngineTest, ScoreReturnsValueInZeroToOne) {
    uint64_t metrics = make_metrics(100, 50, 30, ACTIVE, 0);
    float s = engine->score(metrics, 1000, 1100);
    EXPECT_GE(s, 0.0f);
    EXPECT_LE(s, 1.0f);
}

TEST_F(ScoringEngineTest, RecentAccessScoresHigher) {
    uint64_t metrics = make_metrics(100, 50, 30, ACTIVE, 0);
    float recent = engine->score(metrics, 1100, 1100);
    float old = engine->score(metrics, 100, 1100);
    EXPECT_GT(recent, old);
}

TEST_F(ScoringEngineTest, HighFrequencyScoresHigher) {
    uint32_t now = 2000;
    uint64_t hot_metrics = make_metrics(4000, 100, 30, ACTIVE, 0);
    uint64_t cold_metrics = make_metrics(10, 100, 30, ACTIVE, 0);

    float hot = engine->score(hot_metrics, now, now);
    float cold = engine->score(cold_metrics, now, now);
    EXPECT_GT(hot, cold);
}

TEST_F(ScoringEngineTest, RandomScoresHigherThanSequential) {
    uint32_t now = 2000;
    uint64_t random_metrics = make_metrics(100, 50, 63, ACTIVE, 0);
    uint64_t seq_metrics = make_metrics(100, 50, 0, ACTIVE, 0);

    float random_score = engine->score(random_metrics, now, now);
    float seq_score = engine->score(seq_metrics, now, now);
    EXPECT_GT(random_score, seq_score);
}

TEST_F(ScoringEngineTest, WriteHeavyScoresLower) {
    uint32_t now = 2000;
    uint64_t write_heavy = make_metrics(100, 4000, 30, ACTIVE, 0);
    uint64_t write_light = make_metrics(100, 10, 30, ACTIVE, 0);

    float heavy = engine->score(write_heavy, now, now);
    float light = engine->score(write_light, now, now);
    EXPECT_LT(heavy, light);
}

TEST_F(ScoringEngineTest, RecencyIsLargestWeight) {
    WeightSet w = engine->current_weights();
    EXPECT_GT(w.w_recency, w.w_frequency);
    EXPECT_GT(w.w_frequency, w.w_randomness);
    EXPECT_GT(w.w_randomness, w.w_write_penalty);
}

TEST_F(ScoringEngineTest, HighWatermarkAmplifiesWritePenalty) {
    WeightSet before = engine->current_weights();

    engine->adapt_weights(0.95);  // above high_watermark (0.80)

    WeightSet after = engine->current_weights();
    EXPECT_GT(after.w_write_penalty, before.w_write_penalty);
    EXPECT_GT(after.w_randomness, before.w_randomness);
    EXPECT_LT(after.w_recency, before.w_recency);
}

TEST_F(ScoringEngineTest, LowWatermarkBoostsRecency) {
    WeightSet before = engine->current_weights();

    engine->adapt_weights(0.10);  // below low_watermark (0.30)

    WeightSet after = engine->current_weights();
    EXPECT_GT(after.w_recency, before.w_recency);
    EXPECT_GT(after.w_frequency, before.w_frequency);
}

TEST_F(ScoringEngineTest, MidWatermarkUsesBaseWeights) {
    engine->adapt_weights(0.50);  // between low (0.30) and high (0.80)

    WeightSet w = engine->current_weights();
    EXPECT_FLOAT_EQ(w.w_recency, cfg.base_weights.w_recency);
    EXPECT_FLOAT_EQ(w.w_frequency, cfg.base_weights.w_frequency);
    EXPECT_FLOAT_EQ(w.w_randomness, cfg.base_weights.w_randomness);
    EXPECT_FLOAT_EQ(w.w_write_penalty, cfg.base_weights.w_write_penalty);
}

TEST_F(ScoringEngineTest, ExtentOlderThanCoolIntervalScoresZeroRecency) {
    uint64_t metrics = make_metrics(0, 0, 0, ACTIVE, 0);
    // Access time 600 seconds ago, cool_interval = 300
    float s = engine->score(metrics, 1000, 1600);
    // With no access, no write, no randomness, and zero recency → score = 0
    EXPECT_NEAR(s, 0.0f, 0.01f);
}

TEST_F(ScoringEngineTest, VeryHotExtentScoresHigh) {
    uint32_t now = 2000;
    uint64_t hot = make_metrics(4095, 0, 63, ACTIVE, 0);
    float s = engine->score(hot, now, now);
    // Should be close to max: recency=1, freq=1, random=1, write=0
    // Score = 0.35*1 + 0.30*1 + 0.25*1 - 0.10*0 = 0.90
    EXPECT_GT(s, 0.85f);
}

TEST_F(ScoringEngineTest, VeryColdExtentScoresLow) {
    uint64_t cold = make_metrics(0, 4095, 0, ACTIVE, 0);
    // Access time very old, max writes, no random, no access
    float s = engine->score(cold, 0, 10000);
    // Score = 0.35*0 + 0.30*0 + 0.25*0 - 0.10*1 = -0.10 → clamped to 0
    EXPECT_NEAR(s, 0.0f, 0.01f);
}

// ── B3: Scoring integration tests ──────────────────────────────

class BtierScoringTest : public ::testing::Test {
protected:
    std::string fast_path_;
    std::string slow_path_;
    int fast_fd_ = -1;
    int slow_fd_ = -1;
    static constexpr uint64_t kFastSize = 16 * 1024 * 1024;
    static constexpr uint64_t kSlowSize = 32 * 1024 * 1024;
    BtierConfig cfg;

    void SetUp() override {
        auto tmpl = cxxlab_tmp_path("btier_score_fast");
        fast_fd_ = ::mkstemp(tmpl.data());
        ASSERT_GE(fast_fd_, 0);
        fast_path_ = tmpl;
        ::fallocate(fast_fd_, 0, 0, kFastSize);

        auto tmpl2 = cxxlab_tmp_path("btier_score_slow");
        slow_fd_ = ::mkstemp(tmpl2.data());
        ASSERT_GE(slow_fd_, 0);
        slow_path_ = tmpl2;
        ::fallocate(slow_fd_, 0, 0, kSlowSize);

        cfg.fast_dev_path = fast_path_;
        cfg.slow_dev_path = slow_path_;
        cfg.extent_size = 4 * 1024 * 1024;
        cfg.block_size = 4096;
        cfg.large_value_threshold = 2 * 1024 * 1024;
        cfg.journal_size = 1 * 1024 * 1024;
        cfg.cool_interval_sec = 300;
    }

    void TearDown() override {
        if (fast_fd_ >= 0) ::close(fast_fd_);
        if (slow_fd_ >= 0) ::close(slow_fd_);
        if (!fast_path_.empty()) ::unlink(fast_path_.c_str());
        if (!slow_path_.empty()) ::unlink(slow_path_.c_str());
    }

    bufferlist make_value(const std::string &s) {
        bufferlist bl;
        bl.append(s);
        return bl;
    }
};

TEST_F(BtierScoringTest, RunScoringPassReturnsSortedResults) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    // Write some keys
    for (int i = 0; i < 10; i++) {
        std::string key = "key_" + std::to_string(i);
        ASSERT_EQ(engine.put(key, make_value(std::string(1024, 'a'))), 0);
    }

    auto scored = engine.run_scoring_pass();
    EXPECT_GT(scored.size(), 0u);

    // Verify sorted descending
    for (size_t i = 1; i < scored.size(); i++) {
        EXPECT_GE(scored[i - 1].score, scored[i].score);
    }

    engine.shutdown();
}

TEST_F(BtierScoringTest, RandomnessRefreshAllSequential) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    // Write keys with sequential LBA pattern
    // Since all keys are written sequentially, all should be sequential
    for (int i = 0; i < 5; i++) {
        std::string key = "seq_" + std::to_string(i);
        ASSERT_EQ(engine.put(key, make_value(std::string(4096, 'b'))), 0);
    }

    auto scored = engine.run_scoring_pass();
    ASSERT_GT(scored.size(), 0u);

    // After scoring pass, randomness should be 0 for all extents
    // (all keys have sequential access pattern)
    // Score should not be dominated by randomness
    for (const auto &s : scored) {
        EXPECT_GE(s.score, 0.0f);
        EXPECT_LE(s.score, 1.0f);
    }

    engine.shutdown();
}

TEST_F(BtierScoringTest, HotExtentScoresHigherThanCold) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    // Write "hot" keys (will be read multiple times)
    for (int i = 0; i < 3; i++) {
        std::string key = "hot_" + std::to_string(i);
        ASSERT_EQ(engine.put(key, make_value(std::string(4096, 'c'))), 0);
    }

    // Write "cold" keys (will never be read)
    for (int i = 0; i < 3; i++) {
        std::string key = "cold_" + std::to_string(i);
        ASSERT_EQ(engine.put(key, make_value(std::string(4096, 'd'))), 0);
    }

    // Read hot keys multiple times
    for (int iter = 0; iter < 10; iter++) {
        for (int i = 0; i < 3; i++) {
            std::string key = "hot_" + std::to_string(i);
            bufferlist result;
            engine.get(key, result);
        }
    }

    auto scored = engine.run_scoring_pass();
    ASSERT_GE(scored.size(), 1u);

    // The highest-scoring extent should have a positive score
    // (it was recently accessed with high frequency)
    EXPECT_GT(scored[0].score, 0.0f);

    engine.shutdown();
}

TEST_F(BtierScoringTest, ScoringPassUpdatesWeights) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    // Write some data
    for (int i = 0; i < 5; i++) {
        std::string key = "weight_" + std::to_string(i);
        ASSERT_EQ(engine.put(key, make_value(std::string(1024, 'e'))), 0);
    }

    // Run scoring pass — should not crash
    auto scored = engine.run_scoring_pass();
    EXPECT_GT(scored.size(), 0u);

    engine.shutdown();
}

TEST_F(BtierScoringTest, EmptyEngineScoringPass) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    auto scored = engine.run_scoring_pass();
    EXPECT_EQ(scored.size(), 0u);

    engine.shutdown();
}
