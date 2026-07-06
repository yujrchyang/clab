#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "btier/config.h"
#include "cxxlab_test.h"

using namespace TOPNSPC::btier;

TEST(ConfigTest, DefaultValues) {
    BtierConfig cfg;
    EXPECT_EQ(cfg.extent_size, 4 * 1024 * 1024ULL);
    EXPECT_EQ(cfg.block_size, 4096ULL);
    EXPECT_EQ(cfg.large_value_threshold, 2 * 1024 * 1024ULL);
    EXPECT_FLOAT_EQ(cfg.base_weights.w_recency, 0.35f);
    EXPECT_FLOAT_EQ(cfg.base_weights.w_frequency, 0.30f);
    EXPECT_FLOAT_EQ(cfg.base_weights.w_randomness, 0.25f);
    EXPECT_FLOAT_EQ(cfg.base_weights.w_write_penalty, 0.10f);
    EXPECT_DOUBLE_EQ(cfg.low_watermark, 0.30);
    EXPECT_DOUBLE_EQ(cfg.high_watermark, 0.80);
    EXPECT_EQ(cfg.scan_interval_ms, 1000u);
    EXPECT_EQ(cfg.max_migrations_per_cycle, 16u);
    EXPECT_EQ(cfg.max_compactions_per_cycle, 4u);
    EXPECT_FLOAT_EQ(cfg.promote_threshold, 0.7f);
    EXPECT_FLOAT_EQ(cfg.demote_threshold, 0.3f);
    EXPECT_DOUBLE_EQ(cfg.compaction_dead_ratio, 0.50);
    EXPECT_DOUBLE_EQ(cfg.compaction_usage_ratio, 0.80);
    EXPECT_EQ(cfg.cool_interval_sec, 300u);
    EXPECT_EQ(cfg.sequential_threshold, 64 * 1024ULL);
}

TEST(ConfigTest, SaveLoadRoundtrip) {
    auto tmpl = cxxlab_tmp_path("config_test");
    std::string path(tmpl);

    BtierConfig orig;
    orig.fast_dev_path = "/dev/nvme0n1";
    orig.slow_dev_path = "/dev/sda";
    orig.extent_size = 8 * 1024 * 1024;
    orig.block_size = 512;
    orig.large_value_threshold = 4 * 1024 * 1024;
    orig.base_weights.w_recency = 0.40f;
    orig.base_weights.w_frequency = 0.25f;
    orig.base_weights.w_randomness = 0.20f;
    orig.base_weights.w_write_penalty = 0.15f;
    orig.low_watermark = 0.25;
    orig.high_watermark = 0.85;
    orig.scan_interval_ms = 500;
    orig.max_migrations_per_cycle = 32;
    orig.max_compactions_per_cycle = 8;
    orig.promote_threshold = 0.65f;
    orig.demote_threshold = 0.35f;
    orig.compaction_dead_ratio = 0.40;
    orig.compaction_usage_ratio = 0.90;
    orig.cool_interval_sec = 600;
    orig.sequential_threshold = 128 * 1024;

    int r = orig.save(path);
    ASSERT_EQ(r, 0);

    BtierConfig loaded = BtierConfig::load(path);

    EXPECT_EQ(loaded.fast_dev_path, "/dev/nvme0n1");
    EXPECT_EQ(loaded.slow_dev_path, "/dev/sda");
    EXPECT_EQ(loaded.extent_size, 8 * 1024 * 1024ULL);
    EXPECT_EQ(loaded.block_size, 512ULL);
    EXPECT_EQ(loaded.large_value_threshold, 4 * 1024 * 1024ULL);
    EXPECT_FLOAT_EQ(loaded.base_weights.w_recency, 0.40f);
    EXPECT_FLOAT_EQ(loaded.base_weights.w_frequency, 0.25f);
    EXPECT_FLOAT_EQ(loaded.base_weights.w_randomness, 0.20f);
    EXPECT_FLOAT_EQ(loaded.base_weights.w_write_penalty, 0.15f);
    EXPECT_DOUBLE_EQ(loaded.low_watermark, 0.25);
    EXPECT_DOUBLE_EQ(loaded.high_watermark, 0.85);
    EXPECT_EQ(loaded.scan_interval_ms, 500u);
    EXPECT_EQ(loaded.max_migrations_per_cycle, 32u);
    EXPECT_EQ(loaded.max_compactions_per_cycle, 8u);
    EXPECT_FLOAT_EQ(loaded.promote_threshold, 0.65f);
    EXPECT_FLOAT_EQ(loaded.demote_threshold, 0.35f);
    EXPECT_DOUBLE_EQ(loaded.compaction_dead_ratio, 0.40);
    EXPECT_DOUBLE_EQ(loaded.compaction_usage_ratio, 0.90);
    EXPECT_EQ(loaded.cool_interval_sec, 600u);
    EXPECT_EQ(loaded.sequential_threshold, 128 * 1024ULL);

    ::unlink(path.c_str());
}

TEST(ConfigTest, MissingFieldsUseDefaults) {
    auto tmpl = cxxlab_tmp_path("config_missing");
    std::string path(tmpl);

    // Write a minimal config with only device paths
    FILE *f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs(
        "{\n  \"fast_dev_path\": \"/dev/fast\",\n"
        "  \"slow_dev_path\": \"/dev/slow\"\n}\n",
        f);
    std::fclose(f);

    BtierConfig loaded = BtierConfig::load(path);
    EXPECT_EQ(loaded.fast_dev_path, "/dev/fast");
    EXPECT_EQ(loaded.slow_dev_path, "/dev/slow");
    // Defaults should be used for everything else
    EXPECT_EQ(loaded.extent_size, 4 * 1024 * 1024ULL);
    EXPECT_EQ(loaded.block_size, 4096ULL);
    EXPECT_FLOAT_EQ(loaded.base_weights.w_recency, 0.35f);

    ::unlink(path.c_str());
}

TEST(ConfigTest, UnknownFieldsIgnored) {
    auto tmpl = cxxlab_tmp_path("config_unknown");
    std::string path(tmpl);

    FILE *f = std::fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    std::fputs(
        "{\n"
        "  \"fast_dev_path\": \"/dev/fast\",\n"
        "  \"slow_dev_path\": \"/dev/slow\",\n"
        "  \"unknown_field\": 12345,\n"
        "  \"another_unknown\": {\"nested\": true}\n"
        "}\n",
        f);
    std::fclose(f);

    BtierConfig loaded = BtierConfig::load(path);
    EXPECT_EQ(loaded.fast_dev_path, "/dev/fast");
    EXPECT_EQ(loaded.slow_dev_path, "/dev/slow");
    EXPECT_EQ(loaded.extent_size, 4 * 1024 * 1024ULL);

    ::unlink(path.c_str());
}

TEST(ConfigTest, WeightSetDefaults) {
    WeightSet ws;
    EXPECT_FLOAT_EQ(ws.w_recency, 0.35f);
    EXPECT_FLOAT_EQ(ws.w_frequency, 0.30f);
    EXPECT_FLOAT_EQ(ws.w_randomness, 0.25f);
    EXPECT_FLOAT_EQ(ws.w_write_penalty, 0.10f);
}
