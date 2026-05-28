#include <gtest/gtest.h>

#include <cstdint>

#include "common/page.h"

using clab::page;

TEST(PageTest, SizeIsPositiveAndPowerOfTwo) {
    auto sz = page().size;
    ASSERT_GT(sz, 0u);
    EXPECT_EQ(sz & (sz - 1), 0u);
}

TEST(PageTest, MaskIsConsistentWithSize) {
    auto m = page().mask;
    auto sz = page().size;
    EXPECT_EQ(m & (sz - 1), 0ul);
    EXPECT_EQ(static_cast<unsigned long>(~m) + 1, static_cast<unsigned long>(sz));
}

TEST(PageTest, ShiftIsConsistentWithSize) {
    EXPECT_EQ(1u << page().shift, page().size);
}

TEST(PageTest, AlignedAddressIsMultipleOfSize) {
    auto p = page();
    uintptr_t addr = 0x1234;
    uintptr_t aligned = addr & p.mask;
    EXPECT_EQ(aligned % p.size, 0ul);
}

TEST(PageTest, MaskClearsLowBits) {
    auto p = page();
    uintptr_t addr = p.size + 1;
    uintptr_t aligned = addr & p.mask;
    EXPECT_EQ(aligned, static_cast<uintptr_t>(p.size));
}

TEST(PageTest, ReturnsSameValuesOnRepeatedCalls) {
    auto a = page();
    auto b = page();
    EXPECT_EQ(a.size, b.size);
    EXPECT_EQ(a.mask, b.mask);
    EXPECT_EQ(a.shift, b.shift);
}
