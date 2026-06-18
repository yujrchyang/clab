#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

#include "bluestore/allocator.h"
#include "bluestore/bluestore_types.h"
#include "bluestore/hybrid_allocator.h"
#include "common/intarith.h"

using namespace TOPNSPC;

namespace {

class HybridAllocatorTest : public ::testing::Test {
protected:
    static constexpr int64_t BLOCK_SIZE = 4096;
    static constexpr int64_t DEV_SIZE = 1ULL << 20;  // 1MB (smaller for test speed)

    std::unique_ptr<HybridAllocator> alloc;

    void SetUp() override {
        alloc = std::make_unique<HybridAllocator>(
            DEV_SIZE, BLOCK_SIZE, sizeof(range_seg_t) * 2, "test_hybrid");
    }

    void TearDown() override {
        alloc.reset();
    }

    void add_free(uint64_t offset, uint64_t length) {
        alloc->init_add_free(offset, length);
    }

    void rm_free(uint64_t offset, uint64_t length) {
        alloc->init_rm_free(offset, length);
    }
};

// =====================================================================
// Basic lifecycle
// =====================================================================

TEST_F(HybridAllocatorTest, CreateNoFree) {
    EXPECT_EQ(alloc->get_free(), 0);
    EXPECT_EQ(alloc->get_capacity(), DEV_SIZE);
    EXPECT_EQ(alloc->get_block_size(), BLOCK_SIZE);
    EXPECT_STREQ(alloc->get_type(), "hybrid");
}

TEST_F(HybridAllocatorTest, AddFreeThenAllocate) {
    add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);

    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents), BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE);
}

// =====================================================================
// Simple allocation
// =====================================================================

TEST_F(HybridAllocatorTest, AllocateOneBlock) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents);
    ASSERT_EQ(r, BLOCK_SIZE);
    ASSERT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].offset, 0);
    EXPECT_EQ(extents[0].length, BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE);
}

TEST_F(HybridAllocatorTest, AllocateMultipleBlocks) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE * 10, BLOCK_SIZE, 0, &extents);
    ASSERT_EQ(r, BLOCK_SIZE * 10);
    EXPECT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].offset, 0);
    EXPECT_EQ(extents[0].length, BLOCK_SIZE * 10);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE * 10);
}

// =====================================================================
// Allocation with spillover to bitmap child
// =====================================================================

TEST_F(HybridAllocatorTest, SpilloverToBitmap) {
    // With cap=2, adding 3 ranges should push one to bitmap
    add_free(0, BLOCK_SIZE * 8);
    add_free(BLOCK_SIZE * 16, BLOCK_SIZE * 8);
    add_free(BLOCK_SIZE * 32, BLOCK_SIZE * 8);

    // At least two should be in AVL (accounting for coalescing)
    uint64_t total_free = alloc->get_free();
    EXPECT_EQ(total_free, BLOCK_SIZE * 24);

    // Allocate all free space — should succeed (pulling from both AVL and bitmap)
    PExtentVector extents;
    int64_t r = alloc->allocate(total_free, BLOCK_SIZE, 0, &extents);
    ASSERT_EQ(r, static_cast<int64_t>(total_free));
    EXPECT_EQ(alloc->get_free(), 0);
}

// =====================================================================
// Allocate all, then release all
// =====================================================================

TEST_F(HybridAllocatorTest, AllocateAll) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(DEV_SIZE, BLOCK_SIZE, 0, &extents);
    ASSERT_EQ(r, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), 0);
}

TEST_F(HybridAllocatorTest, AllocateAllThenReleaseAll) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(DEV_SIZE, BLOCK_SIZE, 0, &extents), DEV_SIZE);

    interval_set<uint64_t> release_set;
    for (auto &e : extents)
        release_set.insert(e.offset, e.length);
    alloc->release(release_set);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);

    // Re-allocate after release
    extents.clear();
    ASSERT_EQ(alloc->allocate(DEV_SIZE, BLOCK_SIZE, 0, &extents), DEV_SIZE);
}

// =====================================================================
// ENOSPC
// =====================================================================

TEST_F(HybridAllocatorTest, Enospc) {
    PExtentVector extents;
    EXPECT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents), -ENOSPC);
}

TEST_F(HybridAllocatorTest, AllocateFragmented) {
    add_free(0, BLOCK_SIZE);
    add_free(BLOCK_SIZE * 4, BLOCK_SIZE);
    add_free(BLOCK_SIZE * 8, BLOCK_SIZE);

    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE * 3, BLOCK_SIZE, 0, &extents);
    ASSERT_EQ(r, BLOCK_SIZE * 3);
    EXPECT_EQ(alloc->get_free(), 0);
}

// =====================================================================
// Foreach
// =====================================================================

TEST_F(HybridAllocatorTest, Foreach) {
    add_free(0, BLOCK_SIZE * 4);
    add_free(BLOCK_SIZE * 16, BLOCK_SIZE * 4);

    std::set<uint64_t> seen_starts;
    alloc->foreach ([&](uint64_t offset, uint64_t length) {
        seen_starts.insert(offset);
        (void)length;
    });
    EXPECT_EQ(seen_starts.size(), 2);
}

TEST_F(HybridAllocatorTest, ForeachPartial) {
    add_free(0, DEV_SIZE);

    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(DEV_SIZE / 2, BLOCK_SIZE, 0, &extents), DEV_SIZE / 2);

    uint64_t sum = 0;
    alloc->foreach ([&](uint64_t offset, uint64_t length) {
        EXPECT_GE(offset, DEV_SIZE / 2);
        sum += length;
    });
    EXPECT_EQ(sum, DEV_SIZE / 2);
}

// =====================================================================
// Shutdown and recreate
// =====================================================================

TEST_F(HybridAllocatorTest, ShutdownAndRecreate) {
    add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);

    alloc->shutdown();
    EXPECT_EQ(alloc->get_free(), 0);

    add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

// =====================================================================
// Init rm free
// =====================================================================

TEST_F(HybridAllocatorTest, InitRmFree) {
    add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);

    rm_free(0, BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE);
}

// =====================================================================
// Create via factory
// =====================================================================

TEST_F(HybridAllocatorTest, CreateViaFactory) {
    auto *a = Allocator::create("hybrid", DEV_SIZE, BLOCK_SIZE, "test_factory");
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(a->get_type(), "hybrid");
    delete a;
}

// =====================================================================
// Max alloc size
// =====================================================================

TEST_F(HybridAllocatorTest, MaxAllocSize) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(DEV_SIZE, BLOCK_SIZE, BLOCK_SIZE * 4, 0, &extents);
    ASSERT_EQ(r, DEV_SIZE);
    EXPECT_GT(extents.size(), 1);
    uint64_t total = 0;
    for (auto &e : extents) {
        EXPECT_LE(e.length, BLOCK_SIZE * 4);
        total += e.length;
    }
    EXPECT_EQ(total, DEV_SIZE);
}

// =====================================================================
// Multiple free ranges
// =====================================================================

TEST_F(HybridAllocatorTest, MultipleFreeRanges) {
    add_free(BLOCK_SIZE * 0, BLOCK_SIZE * 4);
    add_free(BLOCK_SIZE * 16, BLOCK_SIZE * 4);
    add_free(BLOCK_SIZE * 32, BLOCK_SIZE * 4);
    add_free(BLOCK_SIZE * 48, BLOCK_SIZE * 4);
    // range_count_cap_ = 2, so some spill to bitmap

    EXPECT_EQ(alloc->get_free(), BLOCK_SIZE * 16);

    PExtentVector extents;
    alloc->allocate(BLOCK_SIZE * 16, BLOCK_SIZE, 0, &extents);
    EXPECT_EQ(alloc->get_free(), 0);
    uint64_t total = 0;
    for (auto &e : extents) {
        EXPECT_EQ(e.length % BLOCK_SIZE, 0);
        total += e.length;
    }
    EXPECT_EQ(total, BLOCK_SIZE * 16);
}

// =====================================================================
// init_rm_free when range is in bitmap child
// =====================================================================

TEST_F(HybridAllocatorTest, InitRmFreeFromBitmap) {
    // cap=2, adding 3 ranges spills one to bitmap
    add_free(0, BLOCK_SIZE);
    add_free(BLOCK_SIZE * 16, BLOCK_SIZE);
    add_free(BLOCK_SIZE * 32, BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), BLOCK_SIZE * 3);

    // Remove the range that spilled to bitmap (the third one, at 32KB)
    rm_free(BLOCK_SIZE * 32, BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), BLOCK_SIZE * 2);

    // Can still allocate from the remaining two
    PExtentVector e1;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e1), BLOCK_SIZE);
    PExtentVector e2;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e2), BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), 0);
}

}  // anonymous namespace
