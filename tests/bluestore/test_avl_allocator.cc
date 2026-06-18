#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <vector>

#include "bluestore/allocator.h"
#include "bluestore/avl_allocator.h"
#include "bluestore/bluestore_types.h"
#include "common/intarith.h"

using namespace TOPNSPC;

namespace {

class AvlAllocatorTest : public ::testing::Test {
protected:
    static constexpr int64_t BLOCK_SIZE = 4096;
    static constexpr int64_t DEV_SIZE = 1ULL << 30;  // 1GB

    std::unique_ptr<AvlAllocator> alloc;

    void SetUp() override {
        alloc = std::make_unique<AvlAllocator>(
            DEV_SIZE, BLOCK_SIZE, "test_avl");
    }

    void TearDown() override {
        alloc.reset();
    }

    void add_free(uint64_t offset, uint64_t length) {
        alloc->init_add_free(offset, length);
    }
};

// =====================================================================
// Basic lifecycle
// =====================================================================

TEST_F(AvlAllocatorTest, CreateNoFree) {
    EXPECT_EQ(alloc->get_free(), 0);
    EXPECT_EQ(alloc->get_capacity(), DEV_SIZE);
    EXPECT_EQ(alloc->get_block_size(), BLOCK_SIZE);
    EXPECT_STREQ(alloc->get_type(), "avl");
}

TEST_F(AvlAllocatorTest, AddFreeThenAllocate) {
    add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);

    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents), BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE);
}

// =====================================================================
// Simple allocation
// =====================================================================

TEST_F(AvlAllocatorTest, AllocateOneBlock) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents);
    ASSERT_EQ(r, BLOCK_SIZE);
    ASSERT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].offset, 0);
    EXPECT_EQ(extents[0].length, BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE);
}

TEST_F(AvlAllocatorTest, AllocateMultipleBlocks) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE * 10, BLOCK_SIZE, 0, &extents);
    ASSERT_EQ(r, BLOCK_SIZE * 10);
    EXPECT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].offset, 0);
    EXPECT_EQ(extents[0].length, BLOCK_SIZE * 10);
}

// =====================================================================
// Release and reuse
// =====================================================================

TEST_F(AvlAllocatorTest, AllocateThenRelease) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents), BLOCK_SIZE);
    ASSERT_EQ(extents.size(), 1);

    interval_set<uint64_t> release_set;
    release_set.insert(extents[0].offset, extents[0].length);
    alloc->release(release_set);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

TEST_F(AvlAllocatorTest, ReleaseMergesAdjacent) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE * 2, BLOCK_SIZE, 0, &extents), BLOCK_SIZE * 2);
    ASSERT_EQ(extents.size(), 1);
    uint64_t off = extents[0].offset;

    // Release first block only
    interval_set<uint64_t> rs;
    rs.insert(off, BLOCK_SIZE);
    alloc->release(rs);

    // Allocate a block — should come from the freed block (cursor bucket
    // for 4K-size allocations starts at 0, so first-fit finds [0, 4096))
    PExtentVector extents2;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents2), BLOCK_SIZE);
    EXPECT_EQ(extents2[0].offset, off);
}

// =====================================================================
// Fragmentation — cursor-based first-fit
// =====================================================================

TEST_F(AvlAllocatorTest, AllocateFragmented) {
    add_free(0, DEV_SIZE);
    interval_set<uint64_t> rs;

    // Allocate 3 extents: cursor at lbas_[12] advances 0 → 4096 → 16384 → 20480
    PExtentVector e1;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e1), BLOCK_SIZE);
    EXPECT_EQ(e1[0].offset, 0);

    PExtentVector e2;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE * 3, BLOCK_SIZE, 0, &e2), BLOCK_SIZE * 3);
    EXPECT_EQ(e2[0].offset, BLOCK_SIZE);

    PExtentVector e3;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e3), BLOCK_SIZE);
    EXPECT_EQ(e3[0].offset, BLOCK_SIZE * 4);

    // Release outer extents, leaving e2 as an allocated island at [4096, 16384)
    // Free: [0, 4096), [16384, DEV_SIZE) — merged
    rs.insert(e1[0].offset, e1[0].length);
    rs.insert(e3[0].offset, e3[0].length);
    alloc->release(rs);

    // cursor(lbas_[12]) is now at 20480; first-fit finds [16384, DEV_SIZE)
    PExtentVector e4;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e4), BLOCK_SIZE);
    EXPECT_EQ(e4[0].offset, BLOCK_SIZE * 4);
    EXPECT_EQ(e4[0].length, BLOCK_SIZE);

    // Fragmentation score is non-zero (2 free extents with huge gap between)
    EXPECT_GT(alloc->get_fragmentation(), 0.0);
}

// =====================================================================
// Alignment enforcement when unit > block_size
// =====================================================================

TEST_F(AvlAllocatorTest, AlignmentEnforcement) {
    // Free space starts at BLOCK_SIZE (not 0), cursor bucket is fresh
    add_free(BLOCK_SIZE, DEV_SIZE - BLOCK_SIZE);

    // Allocate with unit = 8K, which is > block_size = 4K.
    // The free segment starts at 4096, which is NOT 8K-aligned.
    // Without the p2roundup fix, _pick_block_after would return 4096 (bug).
    // With the fix, it rounds up to 8192.
    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE * 2,   // want = 8192
                                BLOCK_SIZE * 2,   // unit = 8192
                                0, &extents);
    ASSERT_EQ(r, BLOCK_SIZE * 2);
    ASSERT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].offset, BLOCK_SIZE * 2);  // 8192, aligned to 8192
    EXPECT_EQ(extents[0].length, BLOCK_SIZE * 2);
}

// =====================================================================
// Best-fit path (free_pct < threshold / max_size < threshold)
// =====================================================================

TEST_F(AvlAllocatorTest, BestFitByFreePct) {
    add_free(0, DEV_SIZE);
    // Allocate 99% of space so that free_pct < 4%, forcing best-fit
    uint64_t alloc_size = p2align(
        static_cast<uint64_t>(DEV_SIZE) * 99 / 100,
        static_cast<uint64_t>(BLOCK_SIZE));
    PExtentVector big;
    ASSERT_EQ(alloc->allocate(alloc_size, BLOCK_SIZE, 0, &big), alloc_size);

    // Remaining free ≈ 1%; best-fit should still find a small block
    PExtentVector small;
    int64_t r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &small);
    ASSERT_EQ(r, BLOCK_SIZE);
    ASSERT_EQ(small.size(), 1);
    EXPECT_EQ(small[0].length, BLOCK_SIZE);
    EXPECT_GT(alloc->get_free(), 0);
}

// =====================================================================
// ENOSPC
// =====================================================================

TEST_F(AvlAllocatorTest, Enospc) {
    add_free(0, BLOCK_SIZE);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents), BLOCK_SIZE);

    // Second allocation should fail
    int64_t r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents);
    EXPECT_EQ(r, -ENOSPC);
}

// =====================================================================
// init_rm_free
// =====================================================================

TEST_F(AvlAllocatorTest, InitRmFree) {
    add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);

    alloc->init_rm_free(0, BLOCK_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE);
}

// =====================================================================
// Multiple extents due to max_alloc_size
// =====================================================================

TEST_F(AvlAllocatorTest, MultipleExtentsWithMaxAllocSize) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    // want=64KB, max_alloc_size=32KB → should split into two extents
    int64_t r = alloc->allocate(BLOCK_SIZE * 16, BLOCK_SIZE,
                                BLOCK_SIZE * 8, 0, &extents);
    ASSERT_EQ(r, BLOCK_SIZE * 16);
    ASSERT_EQ(extents.size(), 2);
    EXPECT_EQ(extents[0].length, BLOCK_SIZE * 8);
    EXPECT_EQ(extents[1].length, BLOCK_SIZE * 8);
    EXPECT_EQ(extents[1].offset, extents[0].offset + extents[0].length);
}

// =====================================================================
// Cursor bucket isolation: different size → different cursor
// =====================================================================

TEST_F(AvlAllocatorTest, CursorBucketIsolation) {
    add_free(0, DEV_SIZE);

    // 4K allocation uses lbas_[12] cursor bucket
    PExtentVector small;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &small), BLOCK_SIZE);
    EXPECT_EQ(small[0].offset, 0);
    // cursor(lbas_[12]) = 4096

    // 8K allocation uses lbas_[13] cursor bucket (independent)
    PExtentVector med;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE * 2, BLOCK_SIZE, 0, &med), BLOCK_SIZE * 2);
    EXPECT_EQ(med[0].offset, BLOCK_SIZE);  // starts after the 4K block
    // cursor(lbas_[13]) = 4096 + 8192 = 12288

    // Release the first 4K block → free: [0, 4096), [12288, DEV_SIZE)
    interval_set<uint64_t> rs;
    rs.insert(0, BLOCK_SIZE);
    alloc->release(rs);

    // 4K allocation uses lbas_[12] cursor = 4096 (bucket isolated from 8K)
    // first-fit from 4096 skips [0, 4096), finds [12288, DEV_SIZE) → 12288
    PExtentVector small2;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &small2), BLOCK_SIZE);
    EXPECT_EQ(small2[0].offset, BLOCK_SIZE * 3);  // 12288
}

// =====================================================================
// Shutdown and reuse
// =====================================================================

TEST_F(AvlAllocatorTest, ShutdownAndRecreate) {
    add_free(0, DEV_SIZE);
    alloc->shutdown();
    EXPECT_EQ(alloc->get_free(), 0);
}

// =====================================================================
// Large allocations
// =====================================================================

TEST_F(AvlAllocatorTest, AllocateAll) {
    add_free(0, DEV_SIZE);
    PExtentVector extents;
    int64_t r = alloc->allocate(DEV_SIZE, BLOCK_SIZE, 0, &extents);
    ASSERT_EQ(r, DEV_SIZE);
    EXPECT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].offset, 0);
    EXPECT_EQ(extents[0].length, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), 0);
}

// =====================================================================
// Foreach
// =====================================================================

TEST_F(AvlAllocatorTest, Foreach) {
    add_free(0, BLOCK_SIZE * 10);
    uint64_t total = 0;
    alloc->foreach ([&](uint64_t off, uint64_t len) {
        (void)off;
        total += len;
    });
    EXPECT_EQ(total, BLOCK_SIZE * 10);
}

// =====================================================================
// Fragmentation score
// =====================================================================

TEST_F(AvlAllocatorTest, FragmentationScore) {
    add_free(0, DEV_SIZE);
    double score = alloc->get_fragmentation_score();
    EXPECT_GE(score, 0.0);
    EXPECT_LE(score, 1.0);
    // Single contiguous extent → score should be close to 0.0
    EXPECT_LT(score, 0.01);
}

// =====================================================================
// Foreach after partial allocation
// =====================================================================

TEST_F(AvlAllocatorTest, ForeachPartial) {
    add_free(0, BLOCK_SIZE * 10);
    PExtentVector extents;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE * 3, BLOCK_SIZE, 0, &extents), BLOCK_SIZE * 3);
    // Free: [12288, 40960)
    uint64_t total = 0;
    alloc->foreach ([&](uint64_t off, uint64_t len) {
        (void)off;
        total += len;
    });
    EXPECT_EQ(total, BLOCK_SIZE * 7);
}

// =====================================================================
// Release multiple non-adjacent extents
// =====================================================================

TEST_F(AvlAllocatorTest, ReleaseMultipleNonAdjacent) {
    add_free(0, DEV_SIZE);

    PExtentVector e1, e2, e3;
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e1), BLOCK_SIZE);
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e2), BLOCK_SIZE);
    ASSERT_EQ(alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e3), BLOCK_SIZE);

    interval_set<uint64_t> rs;
    rs.insert(e1[0].offset, e1[0].length);
    rs.insert(e3[0].offset, e3[0].length);
    alloc->release(rs);

    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE);  // e2 still allocated
}

}  // namespace
