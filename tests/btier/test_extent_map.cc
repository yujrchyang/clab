#include <gtest/gtest.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#include "blk/allocator.h"
#include "btier/btier_types.h"
#include "btier/config.h"
#include "btier/extent_map.h"

using namespace TOPNSPC;
using namespace TOPNSPC::btier;

class ExtentMapTest : public ::testing::Test {
protected:
    BtierConfig cfg;
    std::unique_ptr<Allocator> fast_alloc;
    std::unique_ptr<Allocator> slow_alloc;
    std::unique_ptr<ExtentMap> em;

    static constexpr int64_t kFastSize = 4 * 1024 * 1024;   // 4MB
    static constexpr int64_t kSlowSize = 16 * 1024 * 1024;  // 16MB

    void SetUp() override {
        cfg.extent_size = 4 * 1024 * 1024;
        cfg.block_size = 4096;

        fast_alloc.reset(
            Allocator::create("avl", kFastSize, cfg.block_size, "test-fast"));
        slow_alloc.reset(
            Allocator::create("avl", kSlowSize, cfg.block_size, "test-slow"));

        em = std::make_unique<ExtentMap>(cfg);
        em->add_allocator(Tier::FAST, fast_alloc.get());
        em->add_allocator(Tier::SLOW, slow_alloc.get());
        em->init_free_space();
    }

    void TearDown() override {
        em.reset();
        fast_alloc.reset();
        slow_alloc.reset();
    }
};

// ── Basic lifecycle ─────────────────────────────────────────────

TEST_F(ExtentMapTest, AllocateAndGetLocation) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->extent_id, 1u);

    auto loc = em->get_location(result->extent_id);
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(loc->tier, Tier::FAST);
    EXPECT_GT(loc->length, 0u);
}

TEST_F(ExtentMapTest, FreeExtent) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    em->free(result->extent_id);

    auto loc = em->get_location(result->extent_id);
    EXPECT_FALSE(loc.has_value());
}

TEST_F(ExtentMapTest, FreeAndDeferredRelease) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());
    auto loc = *em->get_location(result->extent_id);

    em->free(result->extent_id);

    // After 2 cycles, space should be released
    em->process_deferred_free();  // cycle 1: entry too new (age 1)
    em->process_deferred_free();  // cycle 2: entry released (age 2)

    // The freed space should now be allocatable again
    auto result2 = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result2.has_value());
}

TEST_F(ExtentMapTest, Snapshot) {
    em->allocate_extent(Tier::FAST, 4096);
    em->allocate_extent(Tier::SLOW, 4096);

    auto snaps = em->snapshot();
    EXPECT_EQ(snaps.size(), 2u);

    int fast_count = 0, slow_count = 0;
    for (const auto &s : snaps) {
        if (s.location.tier == Tier::FAST)
            fast_count++;
        else
            slow_count++;
    }
    EXPECT_EQ(fast_count, 1);
    EXPECT_EQ(slow_count, 1);
}

// ── Multi-key packing ──────────────────────────────────────────

TEST_F(ExtentMapTest, AppendSlotUntilFull) {
    auto result = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
    ASSERT_TRUE(result.has_value());

    // Append small values until full
    uint32_t val_size = 4096;
    uint32_t offset = 0;
    int count = 0;
    while (true) {
        offset = em->append_slot(result->extent_id, val_size);
        if (offset == UINT32_MAX) break;
        count++;
    }

    // 4MB extent - 4KB header = 4MB - 4KB data area
    // 4MB / 4KB = 1024 - 1 = 1023 slots
    EXPECT_GT(count, 0);
    EXPECT_LT(count, 1025);
}

TEST_F(ExtentMapTest, AppendSlotBumpsGeneration) {
    auto result = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
    ASSERT_TRUE(result.has_value());

    uint64_t raw_before = em->get_raw_metrics(result->extent_id);
    uint64_t gen_before = ExtentMetrics::generation(raw_before);

    em->append_slot(result->extent_id, 4096);

    uint64_t raw_after = em->get_raw_metrics(result->extent_id);
    uint64_t gen_after = ExtentMetrics::generation(raw_after);

    EXPECT_EQ(gen_after, gen_before + 1);
}

TEST_F(ExtentMapTest, MarkDeadSlotBumpsGeneration) {
    auto result = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
    ASSERT_TRUE(result.has_value());

    em->append_slot(result->extent_id, 4096);

    uint64_t raw_before = em->get_raw_metrics(result->extent_id);
    uint64_t gen_before = ExtentMetrics::generation(raw_before);

    em->mark_dead_slot(result->extent_id, 4096);

    uint64_t raw_after = em->get_raw_metrics(result->extent_id);
    uint64_t gen_after = ExtentMetrics::generation(raw_after);

    EXPECT_EQ(gen_after, gen_before + 1);
}

TEST_F(ExtentMapTest, MarkDeadSlotDecrementsLiveBytes) {
    auto result = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
    ASSERT_TRUE(result.has_value());

    em->append_slot(result->extent_id, 4096);
    EXPECT_EQ(em->get_live_bytes(result->extent_id), 4096u);
    EXPECT_EQ(em->get_used_bytes(result->extent_id), 4096u);

    em->mark_dead_slot(result->extent_id, 4096);
    EXPECT_EQ(em->get_live_bytes(result->extent_id), 0u);
    EXPECT_EQ(em->get_used_bytes(result->extent_id), 4096u);  // used unchanged
}

TEST_F(ExtentMapTest, FindExtentWithSpace) {
    auto result = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
    ASSERT_TRUE(result.has_value());

    // Should find the extent we just allocated
    uint64_t found = em->find_extent_with_space(Tier::FAST, 4096);
    EXPECT_EQ(found, result->extent_id);
}

TEST_F(ExtentMapTest, FindExtentWithSpaceSkipsMigrating) {
    auto result = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
    ASSERT_TRUE(result.has_value());

    // Begin migration — extent should be skipped
    auto h = em->begin_migration(result->extent_id);
    ASSERT_NE(h, nullptr);

    uint64_t found = em->find_extent_with_space(Tier::FAST, 4096);
    EXPECT_EQ(found, UINT64_MAX);  // no suitable extent (migrating skipped)

    em->abort_migration(h.get());
}

// ── Metrics ────────────────────────────────────────────────────

TEST_F(ExtentMapTest, RecordIoUpdatesAccessCount) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    em->record_io(result->extent_id, IoOp::READ, 1000);
    em->record_io(result->extent_id, IoOp::READ, 1001);

    uint64_t raw = em->get_raw_metrics(result->extent_id);
    EXPECT_EQ(ExtentMetrics::access_count(raw), 2u);
    EXPECT_EQ(ExtentMetrics::write_count(raw), 0u);
}

TEST_F(ExtentMapTest, RecordIoWriteUpdatesWriteCount) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    em->record_io(result->extent_id, IoOp::WRITE, 1000);

    uint64_t raw = em->get_raw_metrics(result->extent_id);
    EXPECT_EQ(ExtentMetrics::access_count(raw), 1u);
    EXPECT_EQ(ExtentMetrics::write_count(raw), 1u);
}

TEST_F(ExtentMapTest, RecordIoDoesNotBumpGeneration) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    uint64_t raw_before = em->get_raw_metrics(result->extent_id);
    uint64_t gen_before = ExtentMetrics::generation(raw_before);

    em->record_io(result->extent_id, IoOp::READ, 1000);
    em->record_io(result->extent_id, IoOp::WRITE, 1000);

    uint64_t raw_after = em->get_raw_metrics(result->extent_id);
    uint64_t gen_after = ExtentMetrics::generation(raw_after);

    EXPECT_EQ(gen_after, gen_before);
}

TEST_F(ExtentMapTest, SetRandomnessDoesNotBumpGeneration) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    uint64_t raw_before = em->get_raw_metrics(result->extent_id);
    uint64_t gen_before = ExtentMetrics::generation(raw_before);

    em->set_randomness(result->extent_id, 42);

    uint64_t raw_after = em->get_raw_metrics(result->extent_id);
    EXPECT_EQ(ExtentMetrics::randomness(raw_after), 42u);
    EXPECT_EQ(ExtentMetrics::generation(raw_after), gen_before);
}

// ── Migration handle ────────────────────────────────────────────

TEST_F(ExtentMapTest, BeginMigrationSetsMigratingState) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    auto h = em->begin_migration(result->extent_id);
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->extent_id, result->extent_id);

    uint64_t raw = em->get_raw_metrics(result->extent_id);
    EXPECT_EQ(ExtentMetrics::state(raw), (uint32_t)MIGRATING);

    em->abort_migration(h.get());

    raw = em->get_raw_metrics(result->extent_id);
    EXPECT_EQ(ExtentMetrics::state(raw), (uint32_t)ACTIVE);
}

TEST_F(ExtentMapTest, CommitMigrationUpdatesLocation) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    auto h = em->begin_migration(result->extent_id);
    ASSERT_NE(h, nullptr);

    auto old_loc = *em->get_location(result->extent_id);

    // Allocate new destination
    auto new_loc = em->allocate_raw(Tier::SLOW, old_loc.length);
    ASSERT_TRUE(new_loc.has_value());

    bool committed = em->commit_migration(h.get(), *new_loc);
    EXPECT_TRUE(committed);

    auto current_loc = em->get_location(result->extent_id);
    ASSERT_TRUE(current_loc.has_value());
    EXPECT_EQ(current_loc->offset, new_loc->offset);
    EXPECT_EQ(current_loc->tier, Tier::SLOW);

    // State should be ACTIVE again
    uint64_t raw = em->get_raw_metrics(result->extent_id);
    EXPECT_EQ(ExtentMetrics::state(raw), (uint32_t)ACTIVE);
}

TEST_F(ExtentMapTest, CommitMigrationInterruptedReturnsFalse) {
    auto result = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
    ASSERT_TRUE(result.has_value());

    // Append a slot first (while ACTIVE)
    em->append_slot(result->extent_id, 4096);

    auto h = em->begin_migration(result->extent_id);
    ASSERT_NE(h, nullptr);

    // mark_dead_slot bumps generation even during MIGRATING
    em->mark_dead_slot(result->extent_id, 4096);

    auto new_loc = em->allocate_raw(Tier::SLOW, 4096);
    ASSERT_TRUE(new_loc.has_value());

    bool committed = em->commit_migration(h.get(), *new_loc);
    EXPECT_FALSE(committed);  // interrupted — gen changed

    em->abort_migration(h.get());
    em->release_source(*new_loc);
}

TEST_F(ExtentMapTest, TwoBeginMigrationSameExtent) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    auto h1 = em->begin_migration(result->extent_id);
    ASSERT_NE(h1, nullptr);

    auto h2 = em->begin_migration(result->extent_id);
    EXPECT_EQ(h2, nullptr);  // already migrating

    em->abort_migration(h1.get());
}

TEST_F(ExtentMapTest, CheckMigrationReturnsTrueWhenUnchanged) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    auto h = em->begin_migration(result->extent_id);
    ASSERT_NE(h, nullptr);

    EXPECT_TRUE(em->check_migration(*h));

    em->abort_migration(h.get());
}

TEST_F(ExtentMapTest, CheckMigrationReturnsFalseWhenChanged) {
    auto result = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
    ASSERT_TRUE(result.has_value());

    // Append a slot first (while ACTIVE)
    em->append_slot(result->extent_id, 4096);

    auto h = em->begin_migration(result->extent_id);
    ASSERT_NE(h, nullptr);

    // mark_dead_slot bumps generation even during MIGRATING
    em->mark_dead_slot(result->extent_id, 4096);

    EXPECT_FALSE(em->check_migration(*h));

    em->abort_migration(h.get());
}

// ── I/O reference counting ─────────────────────────────────────

TEST_F(ExtentMapTest, IoRefIncDec) {
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());

    em->io_ref_inc(result->extent_id);
    em->io_ref_inc(result->extent_id);

    // Should not crash — just atomics
    em->io_ref_dec(result->extent_id);
    em->io_ref_dec(result->extent_id);
}

// ── Watermark ──────────────────────────────────────────────────

TEST_F(ExtentMapTest, FastWatermark) {
    // Initially all free → watermark = 0
    EXPECT_NEAR(em->fast_watermark(), 0.0, 0.01);

    em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);

    // Some space used → watermark > 0
    EXPECT_GT(em->fast_watermark(), 0.0);
}

// ── FAST→SLOW fallback ─────────────────────────────────────────

TEST_F(ExtentMapTest, FastToSlowFallback) {
    // Fill up FAST device — allocate extents and check they're on FAST
    while (true) {
        auto r = em->allocate_extent(Tier::FAST, 4 * 1024 * 1024);
        if (!r.has_value()) break;
        auto loc = em->get_location(r->extent_id);
        if (loc->tier != Tier::FAST) {
            // Fallback already happened — free and stop
            em->free(r->extent_id);
            break;
        }
    }

    // Next allocation should fall back to SLOW
    auto result = em->allocate_extent(Tier::FAST, 4096);
    ASSERT_TRUE(result.has_value());
    auto loc = em->get_location(result->extent_id);
    ASSERT_TRUE(loc.has_value());
    EXPECT_EQ(loc->tier, Tier::SLOW);
}
