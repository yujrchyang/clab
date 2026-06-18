#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <vector>

#include "bluestore/allocator.h"
#include "bluestore/bitmap_allocator.h"
#include "bluestore/bluestore_types.h"
#include "common/intarith.h"

using namespace TOPNSPC;

namespace {

class BitmapAllocatorTest : public ::testing::Test {
protected:
    static constexpr int64_t ALLOC_UNIT = 4096;
    static constexpr int64_t DEV_SIZE = 1ULL << 30;  // 1GB

    std::unique_ptr<BitmapAllocator> alloc;

    void SetUp() override {
        alloc = std::make_unique<BitmapAllocator>(
            DEV_SIZE, ALLOC_UNIT, "test_bitmap");
    }

    void TearDown() override { alloc.reset(); }

    void add_free(uint64_t offset, uint64_t length) {
        alloc->init_add_free(offset, length);
    }
};

// =====================================================================
// Basic lifecycle
// =====================================================================

TEST_F(BitmapAllocatorTest, CreateNoFree) {
    EXPECT_EQ(alloc->get_free(), 0);
    EXPECT_EQ(alloc->get_capacity(), DEV_SIZE);
    EXPECT_EQ(alloc->get_block_size(), ALLOC_UNIT);
    EXPECT_STREQ(alloc->get_type(), "bitmap");
}

TEST_F(BitmapAllocatorTest, AddFreeThenCheckAvailable) {
    add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

// =====================================================================
// Simple allocation
// =====================================================================

TEST_F(BitmapAllocatorTest, AllocateOneBlock) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0, &extents);
    ASSERT_EQ(r, ALLOC_UNIT);
    ASSERT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].offset, 0);
    EXPECT_EQ(extents[0].length, ALLOC_UNIT);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - ALLOC_UNIT);
}

TEST_F(BitmapAllocatorTest, AllocateMultipleBlocks) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(ALLOC_UNIT * 10, ALLOC_UNIT, 0, &extents);
    ASSERT_EQ(r, ALLOC_UNIT * 10);
    EXPECT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].offset, 0);
    EXPECT_EQ(extents[0].length, ALLOC_UNIT * 10);
}

// =====================================================================
// Release and reuse
// =====================================================================

TEST_F(BitmapAllocatorTest, AllocateThenRelease) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0, &extents), ALLOC_UNIT);
    ASSERT_EQ(extents.size(), 1);

    interval_set<uint64_t> rs;
    rs.insert(extents[0].offset, extents[0].length);
    alloc->release(rs);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

TEST_F(BitmapAllocatorTest, ReleaseMergesCorrectly) {
    add_free(0, DEV_SIZE);
    PExtentVector e1, e2;
    ASSERT_EQ(alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0, &e1), ALLOC_UNIT);
    ASSERT_EQ(alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0, &e2), ALLOC_UNIT);

    interval_set<uint64_t> rs;
    rs.insert(e1[0].offset, e1[0].length);
    rs.insert(e2[0].offset, e2[0].length);
    alloc->release(rs);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

// =====================================================================
// init_rm_free
// =====================================================================

TEST_F(BitmapAllocatorTest, InitRmFree) {
    add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);

    alloc->init_rm_free(0, ALLOC_UNIT);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - ALLOC_UNIT);
}

// =====================================================================
// ENOSPC
// =====================================================================

TEST_F(BitmapAllocatorTest, Enospc) {
    add_free(0, ALLOC_UNIT);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0, &extents), ALLOC_UNIT);

    // Second allocation should fail
    int64_t r = alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0, &extents);
    EXPECT_EQ(r, -ENOSPC);
}

// =====================================================================
// Exhaustive: allocate and release all space
// =====================================================================

TEST_F(BitmapAllocatorTest, AllocateAll) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(DEV_SIZE, ALLOC_UNIT, 0, &extents);
    ASSERT_EQ(r, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), 0);
}

TEST_F(BitmapAllocatorTest, AllocateAllThenReleaseAll) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(DEV_SIZE, ALLOC_UNIT, 0, &extents), DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), 0);

    interval_set<uint64_t> rs;
    for (auto &e : extents)
        rs.insert(e.offset, e.length);
    alloc->release(rs);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

// =====================================================================
// Fragmentation — allocate checkerboard, release half
// =====================================================================

TEST_F(BitmapAllocatorTest, AllocateFragmented) {
    add_free(0, DEV_SIZE);
    constexpr int count = 100;

    // Allocate every other block to create fragmentation
    std::vector<PExtentVector> allocs(count);
    for (int i = 0; i < count; ++i) {
        ASSERT_EQ(alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0, &allocs[i]),
                  ALLOC_UNIT);
    }

    // Release even-indexed blocks
    interval_set<uint64_t> rs;
    for (int i = 0; i < count; i += 2)
        rs.insert(allocs[i][0].offset, allocs[i][0].length);
    alloc->release(rs);

    // Verify free space
    EXPECT_EQ(alloc->get_free(),
              DEV_SIZE - static_cast<int64_t>(count / 2) * ALLOC_UNIT);

    // Fragmentation should be non-zero
    EXPECT_GT(alloc->get_fragmentation(), 0.0);

    // Further allocations should succeed from freed blocks
    for (int i = 0; i < count / 2; ++i) {
        PExtentVector e;
        ASSERT_EQ(alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0, &e), ALLOC_UNIT);
    }
    // 50 odd blocks + 50 re-allocated evens = 100 blocks allocated
    EXPECT_EQ(alloc->get_free(),
              DEV_SIZE - static_cast<int64_t>(count) * ALLOC_UNIT);
}

// =====================================================================
// max_alloc_size causes extent splitting
// =====================================================================

TEST_F(BitmapAllocatorTest, MaxAllocSize) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(ALLOC_UNIT * 16, ALLOC_UNIT,
                                ALLOC_UNIT * 8, 0, &extents);
    ASSERT_EQ(r, ALLOC_UNIT * 16);
    // Should be split into at least 2 extents of 8 blocks each
    ASSERT_GE(extents.size(), 2);
    uint64_t total = 0;
    for (auto &e : extents) {
        EXPECT_LE(e.length, ALLOC_UNIT * 8);
        total += e.length;
    }
    EXPECT_EQ(total, ALLOC_UNIT * 16);
}

// =====================================================================
// Foreach
// =====================================================================

TEST_F(BitmapAllocatorTest, Foreach) {
    add_free(0, ALLOC_UNIT * 10);
    uint64_t total = 0;
    alloc->foreach ([&](uint64_t off, uint64_t len) {
        (void)off;
        total += len;
    });
    EXPECT_EQ(total, ALLOC_UNIT * 10);
}

TEST_F(BitmapAllocatorTest, ForeachPartial) {
    add_free(0, ALLOC_UNIT * 10);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(ALLOC_UNIT * 3, ALLOC_UNIT, 0, &extents),
              ALLOC_UNIT * 3);
    uint64_t total = 0;
    alloc->foreach ([&](uint64_t off, uint64_t len) {
        (void)off;
        total += len;
    });
    EXPECT_EQ(total, ALLOC_UNIT * 7);
}

// =====================================================================
// Shutdown
// =====================================================================

TEST_F(BitmapAllocatorTest, ShutdownAndRecreate) {
    add_free(0, DEV_SIZE);
    alloc->shutdown();
    EXPECT_EQ(alloc->get_free(), 0);
}

// =====================================================================
// Hint handling
// =====================================================================

TEST_F(BitmapAllocatorTest, HintPosition) {
    add_free(0, DEV_SIZE);
    // Exhaust first half to force second-round scan
    PExtentVector first;
    ASSERT_EQ(alloc->allocate(DEV_SIZE / 2, ALLOC_UNIT, 0, &first),
              DEV_SIZE / 2);

    // Hint at a position in the second half
    PExtentVector second;
    ASSERT_EQ(alloc->allocate(ALLOC_UNIT, ALLOC_UNIT, 0,
                              DEV_SIZE / 2 + ALLOC_UNIT, &second),
              ALLOC_UNIT);
    EXPECT_GE(second[0].offset, static_cast<uint64_t>(DEV_SIZE / 2));
}

// =====================================================================
// Concurrent init_add_free (sequential, but tests non-first-block adds)
// =====================================================================

TEST_F(BitmapAllocatorTest, InitAddFreeMultipleRanges) {
    alloc->init_add_free(0, ALLOC_UNIT * 10);
    alloc->init_add_free(ALLOC_UNIT * 100, ALLOC_UNIT * 10);
    EXPECT_EQ(alloc->get_free(), ALLOC_UNIT * 20);

    // Drain both ranges (10 blocks total)
    PExtentVector e1, e2;
    ASSERT_EQ(alloc->allocate(ALLOC_UNIT * 5, ALLOC_UNIT, 0, &e1),
              ALLOC_UNIT * 5);
    EXPECT_EQ(e1[0].offset, 0);

    ASSERT_EQ(alloc->allocate(ALLOC_UNIT * 5, ALLOC_UNIT, 0, &e2),
              ALLOC_UNIT * 5);
    // Second allocation may come from either range (contiguous gap in first)
    EXPECT_TRUE(e2[0].offset == ALLOC_UNIT * 5 ||
                e2[0].offset == ALLOC_UNIT * 100);

    EXPECT_EQ(alloc->get_free(), ALLOC_UNIT * 10);
}

// =====================================================================
// Factory creation
// =====================================================================

TEST_F(BitmapAllocatorTest, CreateViaFactory) {
    Allocator *a = Allocator::create("bitmap", DEV_SIZE, ALLOC_UNIT, "factory");
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(a->get_type(), "bitmap");
    EXPECT_EQ(a->get_free(), 0);
    delete a;
}

}  // namespace
