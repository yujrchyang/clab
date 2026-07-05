#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "btier/btier.h"
#include "btier/btier_types.h"
#include "clab_test.h"
#include "common/buffer.h"

using namespace TOPNSPC;
using namespace TOPNSPC::btier;

class BtierEngineTest : public ::testing::Test {
protected:
    std::string fast_path_;
    std::string slow_path_;
    int fast_fd_ = -1;
    int slow_fd_ = -1;
    static constexpr uint64_t kFastSize = 16 * 1024 * 1024;  // 16MB
    static constexpr uint64_t kSlowSize = 32 * 1024 * 1024;  // 32MB

    BtierConfig cfg;

    void SetUp() override {
        auto tmpl = clab_tmp_path("btier_fast");
        fast_fd_ = ::mkstemp(tmpl.data());
        ASSERT_GE(fast_fd_, 0);
        fast_path_ = tmpl;
        ::fallocate(fast_fd_, 0, 0, kFastSize);

        auto tmpl2 = clab_tmp_path("btier_slow");
        slow_fd_ = ::mkstemp(tmpl2.data());
        ASSERT_GE(slow_fd_, 0);
        slow_path_ = tmpl2;
        ::fallocate(slow_fd_, 0, 0, kSlowSize);

        cfg.fast_dev_path = fast_path_;
        cfg.slow_dev_path = slow_path_;
        cfg.extent_size = 4 * 1024 * 1024;
        cfg.block_size = 4096;
        cfg.large_value_threshold = 2 * 1024 * 1024;
        cfg.journal_size = 1 * 1024 * 1024;  // 1MB journal for testing
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

    std::string read_value(bufferlist &bl) {
        return std::string(bl.c_str(), bl.length());
    }
};

// ── Basic init + put + get ─────────────────────────────────────

TEST_F(BtierEngineTest, InitPutGet) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    auto val = make_value("hello world");
    ASSERT_EQ(engine.put("key1", val), 0);

    bufferlist result;
    ASSERT_EQ(engine.get("key1", result), 0);
    EXPECT_EQ(read_value(result), "hello world");

    engine.shutdown();
}

TEST_F(BtierEngineTest, GetNonExistent) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    bufferlist result;
    EXPECT_EQ(engine.get("nonexistent", result), -ENOENT);

    engine.shutdown();
}

// ── Multi-key packing ──────────────────────────────────────────

TEST_F(BtierEngineTest, MultiKeyPacking) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    // Write 100 small values (4KB each)
    for (int i = 0; i < 100; i++) {
        std::string key = "key_" + std::to_string(i);
        std::string val(4096, 'a' + (i % 26));
        ASSERT_EQ(engine.put(key, make_value(val)), 0);
    }

    // Verify all keys
    for (int i = 0; i < 100; i++) {
        std::string key = "key_" + std::to_string(i);
        std::string expected(4096, 'a' + (i % 26));
        bufferlist result;
        ASSERT_EQ(engine.get(key, result), 0);
        EXPECT_EQ(read_value(result), expected);
    }

    auto stats = engine.get_stats();
    EXPECT_EQ(stats.num_keys, 100u);
    // Should be packed into a few extents, not 100
    EXPECT_LT(stats.num_extents, 50u);

    engine.shutdown();
}

// ── Large value → dedicated extent ─────────────────────────────

TEST_F(BtierEngineTest, LargeValueDedicatedExtent) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    // Value >= large_value_threshold (2MB)
    std::string large_val(3 * 1024 * 1024, 'X');
    ASSERT_EQ(engine.put("large", make_value(large_val)), 0);

    bufferlist result;
    ASSERT_EQ(engine.get("large", result), 0);
    EXPECT_EQ(result.length(), large_val.size());

    engine.shutdown();
}

// ── Overwrite ──────────────────────────────────────────────────

TEST_F(BtierEngineTest, OverwriteKey) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    ASSERT_EQ(engine.put("key1", make_value("old_value")), 0);
    ASSERT_EQ(engine.put("key1", make_value("new_value")), 0);

    bufferlist result;
    ASSERT_EQ(engine.get("key1", result), 0);
    EXPECT_EQ(read_value(result), "new_value");

    engine.shutdown();
}

// ── Delete ─────────────────────────────────────────────────────

TEST_F(BtierEngineTest, DeleteKey) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    ASSERT_EQ(engine.put("key1", make_value("value1")), 0);
    ASSERT_EQ(engine.del("key1"), 0);

    bufferlist result;
    EXPECT_EQ(engine.get("key1", result), -ENOENT);

    engine.shutdown();
}

TEST_F(BtierEngineTest, DeleteNonExistent) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    EXPECT_EQ(engine.del("nonexistent"), -ENOENT);

    engine.shutdown();
}

// ── Persistence (crash recovery) ────────────────────────────────

TEST_F(BtierEngineTest, PersistenceAfterRestart) {
    {
        BtierEngine engine;
        ASSERT_EQ(engine.init(cfg), 0);

        for (int i = 0; i < 50; i++) {
            std::string key = "persist_" + std::to_string(i);
            std::string val(4096, 'b');
            ASSERT_EQ(engine.put(key, make_value(val)), 0);
        }

        engine.shutdown();
    }

    // Restart and verify all keys
    {
        BtierEngine engine;
        ASSERT_EQ(engine.init(cfg), 0);

        for (int i = 0; i < 50; i++) {
            std::string key = "persist_" + std::to_string(i);
            bufferlist result;
            ASSERT_EQ(engine.get(key, result), 0);
            EXPECT_EQ(result.length(), 4096u);
            EXPECT_EQ(result[0], 'b');
        }

        engine.shutdown();
    }
}

// ── Stats ──────────────────────────────────────────────────────

TEST_F(BtierEngineTest, GetStats) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    for (int i = 0; i < 10; i++) {
        std::string key = "stat_" + std::to_string(i);
        ASSERT_EQ(engine.put(key, make_value(std::string(4096, 'c'))), 0);
    }

    auto stats = engine.get_stats();
    EXPECT_EQ(stats.num_keys, 10u);
    EXPECT_GT(stats.num_extents, 0u);
    EXPECT_GE(stats.fast_watermark, 0.0);

    engine.shutdown();
}

// ── Sync ───────────────────────────────────────────────────────

TEST_F(BtierEngineTest, SyncAfterWrite) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    ASSERT_EQ(engine.put("key1", make_value("sync_test")), 0);
    ASSERT_EQ(engine.sync(), 0);

    bufferlist result;
    ASSERT_EQ(engine.get("key1", result), 0);
    EXPECT_EQ(read_value(result), "sync_test");

    engine.shutdown();
}

// ── Multiple keys + overwrite + delete ─────────────────────────

TEST_F(BtierEngineTest, MixedOperations) {
    BtierEngine engine;
    ASSERT_EQ(engine.init(cfg), 0);

    // Put 20 keys
    for (int i = 0; i < 20; i++) {
        std::string key = "mix_" + std::to_string(i);
        ASSERT_EQ(engine.put(key, make_value(std::string(1024, 'd'))), 0);
    }

    // Overwrite first 10
    for (int i = 0; i < 10; i++) {
        std::string key = "mix_" + std::to_string(i);
        ASSERT_EQ(engine.put(key, make_value(std::string(2048, 'e'))), 0);
    }

    // Delete first 5
    for (int i = 0; i < 5; i++) {
        std::string key = "mix_" + std::to_string(i);
        ASSERT_EQ(engine.del(key), 0);
    }

    // Verify
    for (int i = 0; i < 5; i++) {
        std::string key = "mix_" + std::to_string(i);
        bufferlist result;
        EXPECT_EQ(engine.get(key, result), -ENOENT);
    }
    for (int i = 5; i < 10; i++) {
        std::string key = "mix_" + std::to_string(i);
        bufferlist result;
        ASSERT_EQ(engine.get(key, result), 0);
        EXPECT_EQ(result.length(), 2048u);
        EXPECT_EQ(result[0], 'e');
    }
    for (int i = 10; i < 20; i++) {
        std::string key = "mix_" + std::to_string(i);
        bufferlist result;
        ASSERT_EQ(engine.get(key, result), 0);
        EXPECT_EQ(result.length(), 1024u);
        EXPECT_EQ(result[0], 'd');
    }

    engine.shutdown();
}
