#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "common/inline_memory.h"

class MemcpyTest : public ::testing::Test {
protected:
    static constexpr size_t kBufSize = 128;
    char src[kBufSize];
    char dst[kBufSize];
    void SetUp() override {
        for (size_t i = 0; i < kBufSize; i++) {
            src[i] = static_cast<char>(i);
        }
        std::memset(dst, 0xFF, kBufSize);
    }
};

TEST_F(MemcpyTest, SizeZero) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 0, 8), dst);
    EXPECT_EQ(static_cast<int>(dst[0]), 0xFF);
}

TEST_F(MemcpyTest, SizeOne) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 1, 8), dst);
    EXPECT_EQ(dst[0], src[0]);
    EXPECT_EQ(static_cast<int>(dst[1]), 0xFF);
}

TEST_F(MemcpyTest, SizeTwo) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 2, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 2), 0);
}

TEST_F(MemcpyTest, SizeThree) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 3, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 3), 0);
}

TEST_F(MemcpyTest, SizeFour) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 4, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 4), 0);
}

TEST_F(MemcpyTest, SizeEight) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 8, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 8), 0);
}

TEST_F(MemcpyTest, SizeDefaultFallback) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 5, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 5), 0);
}

TEST_F(MemcpyTest, SizeSeven) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 7, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 7), 0);
}

TEST_F(MemcpyTest, SizeNine) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 9, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 9), 0);
}

TEST_F(MemcpyTest, MultipleOfEight) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 32, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 32), 0);
}

TEST_F(MemcpyTest, NotMultiple) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 33, 8), dst);
    EXPECT_EQ(std::memcmp(dst, src, 33), 0);
}

TEST_F(MemcpyTest, InlineLenZero) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 100, 0), dst);
    EXPECT_EQ(std::memcmp(dst, src, 100), 0);
}

TEST_F(MemcpyTest, InlineLenLarge) {
    EXPECT_EQ(maybe_inline_memcpy(dst, src, 4, 64), dst);
    EXPECT_EQ(std::memcmp(dst, src, 4), 0);
}

TEST_F(MemcpyTest, SourceUnmodified) {
    char src_copy[kBufSize];
    std::memcpy(src_copy, src, kBufSize);
    maybe_inline_memcpy(dst, src, 50, 16);
    EXPECT_EQ(std::memcmp(src, src_copy, kBufSize), 0);
}

TEST_F(MemcpyTest, ReturnValue) {
    void *ret = maybe_inline_memcpy(dst, src, 10, 16);
    EXPECT_EQ(ret, dst);
}

class MemIsZeroTest : public ::testing::Test {
protected:
    static constexpr size_t kBufSize = 256;
    char buf[kBufSize];
    void SetUp() override {
        std::memset(buf, 0, kBufSize);
    }
};

TEST_F(MemIsZeroTest, EmptyBuffer) {
    EXPECT_TRUE(mem_is_zero(buf, 0));
}

TEST_F(MemIsZeroTest, OneByteZero) {
    EXPECT_TRUE(mem_is_zero(buf, 1));
}

TEST_F(MemIsZeroTest, OneByteNonZero) {
    buf[0] = 1;
    EXPECT_FALSE(mem_is_zero(buf, 1));
}

TEST_F(MemIsZeroTest, SmallBufferAllZero) {
    EXPECT_TRUE(mem_is_zero(buf, 5));
    EXPECT_TRUE(mem_is_zero(buf, 7));
    EXPECT_TRUE(mem_is_zero(buf, 15));
}

TEST_F(MemIsZeroTest, AllZeroExactAlign) {
    EXPECT_TRUE(mem_is_zero(buf, 16));
    EXPECT_TRUE(mem_is_zero(buf, 32));
    EXPECT_TRUE(mem_is_zero(buf, 64));
}

TEST_F(MemIsZeroTest, LargeBufferAllZero) {
    EXPECT_TRUE(mem_is_zero(buf, 100));
    EXPECT_TRUE(mem_is_zero(buf, 200));
}

TEST_F(MemIsZeroTest, NonZeroFirstByte) {
    buf[0] = 0xAB;
    EXPECT_FALSE(mem_is_zero(buf, 16));
    EXPECT_FALSE(mem_is_zero(buf, 1));
}

TEST_F(MemIsZeroTest, NonZeroLastByte) {
    buf[15] = 0xAB;
    EXPECT_FALSE(mem_is_zero(buf, 16));
}

TEST_F(MemIsZeroTest, NonZeroAtByte15) {
    buf[15] = 0x1;
    EXPECT_FALSE(mem_is_zero(buf, 16));
}

TEST_F(MemIsZeroTest, NonZeroAtByte16) {
    buf[16] = 0x1;
    EXPECT_FALSE(mem_is_zero(buf, 32));
}

TEST_F(MemIsZeroTest, NonZeroAtByte31) {
    buf[31] = 0x1;
    EXPECT_FALSE(mem_is_zero(buf, 32));
}

TEST_F(MemIsZeroTest, NonZeroAtByte32) {
    buf[32] = 0x1;
    EXPECT_FALSE(mem_is_zero(buf, 64));
}

TEST_F(MemIsZeroTest, NonZeroInTail) {
    buf[18] = 0x1;
    EXPECT_FALSE(mem_is_zero(buf, 20));
}

TEST_F(MemIsZeroTest, SingleNonZeroInLargeBuffer) {
    buf[80] = 0x1;
    EXPECT_FALSE(mem_is_zero(buf, 100));
}

TEST_F(MemIsZeroTest, UnalignedPtrAllZero) {
    EXPECT_TRUE(mem_is_zero(buf + 7, 50));
}

TEST_F(MemIsZeroTest, UnalignedPtrNonZeroInAlignLoop) {
    buf[7] = 0x1;
    EXPECT_FALSE(mem_is_zero(buf + 7, 50));
}

TEST_F(MemIsZeroTest, UnalignedPtrNonZeroAfterAlign) {
    buf[7 + 16] = 0x1;
    EXPECT_FALSE(mem_is_zero(buf + 7, 50));
}

TEST_F(MemIsZeroTest, ZeroEverywhere) {
    for (size_t len = 1; len <= 128; len++) {
        EXPECT_TRUE(mem_is_zero(buf, len));
    }
}
