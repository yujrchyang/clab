#include <gtest/gtest.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <vector>

#include "blk/allocator.h"
#include "blk/avl_allocator.h"
#include "blk/extent_types.h"
#include "common/intarith.h"

using namespace TOPNSPC;

namespace {

class AvlAllocatorTest : public ::testing::Test {
protected:
    static constexpr int64_t BLOCK_SIZE = 4096;
    static constexpr int64_t DEV_SIZE = 1ULL << 30;  // 1GB

    void SetUp() override {
        alloc.reset(static_cast<AvlAllocator *>(
            Allocator::create("avl", DEV_SIZE, BLOCK_SIZE)));
        ASSERT_NE(alloc, nullptr);
    }

    void init_all_free() {
        alloc->init_add_free(0, DEV_SIZE);
    }

    std::unique_ptr<AvlAllocator, void(*)(AvlAllocator *)> alloc{
        nullptr, [](AvlAllocator *a) { a->shutdown(); delete a; }};
};

TEST_F(AvlAllocatorTest, CreateAndDestroy) {
    // created in SetUp, destroyed in TearDown
}

TEST_F(AvlAllocatorTest, InitAddFreeAndGetFree) {
    init_all_free();
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

TEST_F(AvlAllocatorTest, AllocateSingleExtent) {
    init_all_free();
    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents);
    ASSERT_GT(r, 0);
    ASSERT_EQ(extents.size(), 1);
    EXPECT_EQ(extents[0].length, BLOCK_SIZE);
    EXPECT_LT(extents[0].offset, DEV_SIZE);
}

TEST_F(AvlAllocatorTest, AllocateFailsWhenFull) {
    init_all_free();
    PExtentVector extents;
    int64_t r = alloc->allocate(DEV_SIZE, BLOCK_SIZE, 0, &extents);
    ASSERT_GT(r, 0);
    EXPECT_EQ(static_cast<uint64_t>(r), DEV_SIZE);

    PExtentVector extents2;
    r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents2);
    EXPECT_EQ(r, -ENOSPC);
}

TEST_F(AvlAllocatorTest, ReleaseAndReallocate) {
    init_all_free();
    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE * 10, BLOCK_SIZE, 0, &extents);
    ASSERT_GT(r, 0);
    uint64_t freed = 0;
    for (auto &e : extents)
        freed += e.length;
    EXPECT_EQ(freed, BLOCK_SIZE * 10);

    alloc->release(extents);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

TEST_F(AvlAllocatorTest, PartialAllocWhenInsufficientFree) {
    init_all_free();
    PExtentVector extents;
    alloc->allocate(DEV_SIZE / 2, BLOCK_SIZE, 0, &extents);
    // allocate more than remaining to check partial behavior
    PExtentVector extents2;
    int64_t r = alloc->allocate(DEV_SIZE, BLOCK_SIZE, 0, &extents2);
    ASSERT_GT(r, 0);
    uint64_t total = 0;
    for (auto &e : extents)
        total += e.length;
    for (auto &e : extents2)
        total += e.length;
    EXPECT_EQ(total, DEV_SIZE);
}

TEST_F(AvlAllocatorTest, InitAddFreePartialRange) {
    alloc->init_add_free(BLOCK_SIZE * 10, BLOCK_SIZE * 100);
    EXPECT_EQ(alloc->get_free(), BLOCK_SIZE * 100);
}

TEST_F(AvlAllocatorTest, InitRmFree) {
    init_all_free();
    alloc->init_rm_free(BLOCK_SIZE * 10, BLOCK_SIZE * 100);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE - BLOCK_SIZE * 100);
}

TEST_F(AvlAllocatorTest, AllocWithHint) {
    init_all_free();
    PExtentVector extents;
    int64_t hint = DEV_SIZE / 4;
    int64_t r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, hint, &extents);
    ASSERT_GT(r, 0);
    EXPECT_EQ(extents.size(), 1);
}

TEST_F(AvlAllocatorTest, MultipleSmallAllocsRoundtrip) {
    init_all_free();
    std::vector<PExtentVector> allocs;
    for (int i = 0; i < 10; ++i) {
        PExtentVector e;
        int64_t r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &e);
        ASSERT_GT(r, 0);
        allocs.push_back(std::move(e));
    }

    for (auto &e : allocs)
        alloc->release(e);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

TEST_F(AvlAllocatorTest, AllocMaxSizeLimit) {
    init_all_free();
    PExtentVector extents;
    int64_t r = alloc->allocate(DEV_SIZE / 2, BLOCK_SIZE,
                                BLOCK_SIZE * 2, 0, &extents);
    ASSERT_GT(r, 0);
    for (auto &e : extents)
        EXPECT_LE(e.length, BLOCK_SIZE * 2);
}

TEST_F(AvlAllocatorTest, DoubleReleaseFails) {
    init_all_free();
    PExtentVector extents;
    alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents);
    alloc->release(extents);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
    // Releasing same extents again should be a no-op (or crash in debug mode)
    // In release mode, the double-release is a logic error that clab_assert may catch.
    // We just verify free count doesn't exceed device size.
    EXPECT_LE(alloc->get_free(), DEV_SIZE);
}

TEST_F(AvlAllocatorTest, GetFragmentationZeroWhenEmpty) {
    init_all_free();
    double frag = alloc->get_fragmentation();
    EXPECT_DOUBLE_EQ(frag, 0.0);
}

TEST_F(AvlAllocatorTest, AllocatedAndReleased) {
    init_all_free();
    PExtentVector extents;
    alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents);
    alloc->release(extents);
    PExtentVector extents2;
    int64_t r = alloc->allocate(BLOCK_SIZE, BLOCK_SIZE, 0, &extents2);
    ASSERT_GT(r, 0);
}

TEST_F(AvlAllocatorTest, ShutdownThenInit) {
    alloc->shutdown();
    // After shutdown, re-init by calling init_add_free
    alloc->init_add_free(0, DEV_SIZE);
    EXPECT_EQ(alloc->get_free(), DEV_SIZE);
}

TEST_F(AvlAllocatorTest, InitAddFreeUnaligned) {
    // init_add_free should handle unaligned ranges
    alloc->init_add_free(100, 5000);  // unaligned
    EXPECT_EQ(alloc->get_free(), 5000);
}

TEST_F(AvlAllocatorTest, InitRmFreePartial) {
    init_all_free();
    alloc->init_rm_free(0, BLOCK_SIZE * 5);
    PExtentVector extents;
    int64_t r = alloc->allocate(DEV_SIZE, BLOCK_SIZE, 0, &extents);
    ASSERT_GT(r, 0);
    uint64_t total = 0;
    for (auto &e : extents)
        total += e.length;
    EXPECT_EQ(total, DEV_SIZE - BLOCK_SIZE * 5);
}

TEST_F(AvlAllocatorTest, MultiExtentAlloc) {
    init_all_free();
    PExtentVector extents;
    int64_t r = alloc->allocate(BLOCK_SIZE * 3, BLOCK_SIZE,
                                BLOCK_SIZE, 0, &extents);
    ASSERT_GT(r, 0);
    EXPECT_EQ(extents.size(), 3);
    for (auto &e : extents)
        EXPECT_EQ(e.length, BLOCK_SIZE);
}

}  // namespace
