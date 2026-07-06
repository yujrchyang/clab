#include <gtest/gtest.h>

#include <climits>
#include <cstdint>
#include <limits>

#include "common/intarith.h"

using namespace TOPNSPC;

TEST(DivRoundUpTest, Basic) {
    EXPECT_EQ(div_round_up(10, 5), 2U);
    EXPECT_EQ(div_round_up(11, 5), 3U);
    EXPECT_EQ(div_round_up(14, 5), 3U);
    EXPECT_EQ(div_round_up(15, 5), 3U);
    EXPECT_EQ(div_round_up(16, 5), 4U);
}

TEST(DivRoundUpTest, NLessThanD) {
    EXPECT_EQ(div_round_up(1, 100), 1U);
    EXPECT_EQ(div_round_up(0, 100), 0U);
}

TEST(DivRoundUpTest, ExactDivision) {
    EXPECT_EQ(div_round_up(100, 100), 1U);
    EXPECT_EQ(div_round_up(200, 100), 2U);
}

TEST(DivRoundUpTest, NearMax) {
    uint32_t n = std::numeric_limits<uint32_t>::max();
    EXPECT_EQ(div_round_up(n, 1U), static_cast<uint64_t>(n));
    EXPECT_EQ(div_round_up(n, 2U), (static_cast<uint64_t>(n) + 1) / 2);
    EXPECT_EQ(div_round_up(n, n), 1U);
}

TEST(DivRoundUpTest, MixedTypes) {
    EXPECT_EQ(div_round_up(10, 3U), 4U);
    EXPECT_EQ(div_round_up(10U, 3), 4U);
}

TEST(RoundDownToTest, Basic) {
    EXPECT_EQ(round_down_to(10, 5), 10U);
    EXPECT_EQ(round_down_to(11, 5), 10U);
    EXPECT_EQ(round_down_to(14, 5), 10U);
    EXPECT_EQ(round_down_to(15, 5), 15U);
    EXPECT_EQ(round_down_to(16, 5), 15U);
}

TEST(RoundDownToTest, NLessThanD) {
    EXPECT_EQ(round_down_to(0, 5), 0U);
    EXPECT_EQ(round_down_to(3, 7), 0U);
}

TEST(RoundDownToTest, ExactMultiple) {
    EXPECT_EQ(round_down_to(7, 7), 7U);
}

TEST(RoundUpToTest, Basic) {
    EXPECT_EQ(round_up_to(10, 5), 10U);
    EXPECT_EQ(round_up_to(11, 5), 15U);
    EXPECT_EQ(round_up_to(14, 5), 15U);
    EXPECT_EQ(round_up_to(15, 5), 15U);
    EXPECT_EQ(round_up_to(16, 5), 20U);
}

TEST(RoundUpToTest, NLessThanD) {
    EXPECT_EQ(round_up_to(0, 5), 0U);
    EXPECT_EQ(round_up_to(3, 7), 7U);
}

TEST(RoundUpToTest, ExactlyAligned) {
    EXPECT_EQ(round_up_to(7, 7), 7U);
    EXPECT_EQ(round_up_to(100, 10), 100U);
}

TEST(ShiftRoundUpTest, Basic) {
    EXPECT_EQ(shift_round_up(0, 2), 0U);
    EXPECT_EQ(shift_round_up(1, 2), 1U);
    EXPECT_EQ(shift_round_up(4, 2), 1U);
    EXPECT_EQ(shift_round_up(5, 2), 2U);
    EXPECT_EQ(shift_round_up(8, 2), 2U);
    EXPECT_EQ(shift_round_up(9, 2), 3U);
}

TEST(Isp2Test, PowersOfTwo) {
    EXPECT_TRUE(isp2(1));
    EXPECT_TRUE(isp2(2));
    EXPECT_TRUE(isp2(4));
    EXPECT_TRUE(isp2(1024));
    EXPECT_TRUE(isp2(1UL << 31));
    EXPECT_TRUE(isp2(1ULL << 63));
}

TEST(Isp2Test, Zero) {
    EXPECT_FALSE(isp2(0));
}

TEST(Isp2Test, NonPowers) {
    EXPECT_FALSE(isp2(3));
    EXPECT_FALSE(isp2(5));
    EXPECT_FALSE(isp2(1023));
    EXPECT_FALSE(isp2(0xFFFF));
}

TEST(P2alignTest, Basic) {
    EXPECT_EQ(p2align((unsigned)0, (unsigned)1024), 0U);
    EXPECT_EQ(p2align((unsigned)1200, (unsigned)1024), 1024U);
    EXPECT_EQ(p2align((unsigned)1024, (unsigned)1024), 1024U);
    EXPECT_EQ(p2align((unsigned)2047, (unsigned)1024), 1024U);
    EXPECT_EQ(p2align((unsigned)2048, (unsigned)1024), 2048U);
}

TEST(P2alignTest, HexValues) {
    EXPECT_EQ(p2align((unsigned)0x1234, (unsigned)0x100), 0x1200U);
    EXPECT_EQ(p2align((unsigned)0x5600, (unsigned)0x100), 0x5600U);
}

TEST(P2phaseTest, Basic) {
    EXPECT_EQ(p2phase((unsigned)0x1234, (unsigned)0x100), 0x34U);
    EXPECT_EQ(p2phase((unsigned)0x5600, (unsigned)0x100), 0x00U);
    EXPECT_EQ(p2phase((unsigned)0, (unsigned)0x100), 0U);
    EXPECT_EQ(p2phase((unsigned)0xFF, (unsigned)0x100), 0xFFU);
    EXPECT_EQ(p2phase((unsigned)0x100, (unsigned)0x100), 0U);
}

TEST(P2nphaseTest, Basic) {
    EXPECT_EQ(p2nphase((unsigned)0x1234, (unsigned)0x100), 0xccU);
    EXPECT_EQ(p2nphase((unsigned)0x5600, (unsigned)0x100), 0x00U);
    EXPECT_EQ(p2nphase((unsigned)0, (unsigned)0x100), 0U);
    EXPECT_EQ(p2nphase((unsigned)0x1, (unsigned)0x100), 0xFFU);
    EXPECT_EQ(p2nphase((unsigned)0xFF, (unsigned)0x100), 0x01U);
    EXPECT_EQ(p2nphase((unsigned)0x100, (unsigned)0x100), 0x00U);
}

TEST(P2roundupTest, Basic) {
    EXPECT_EQ(p2roundup((unsigned)0x1234, (unsigned)0x100), 0x1300U);
    EXPECT_EQ(p2roundup((unsigned)0x5600, (unsigned)0x100), 0x5600U);
    EXPECT_EQ(p2roundup((unsigned)0, (unsigned)0x100), 0U);
    EXPECT_EQ(p2roundup((unsigned)0x1, (unsigned)0x100), 0x100U);
    EXPECT_EQ(p2roundup((unsigned)0xFF, (unsigned)0x100), 0x100U);
    EXPECT_EQ(p2roundup((unsigned)0x100, (unsigned)0x100), 0x100U);
    EXPECT_EQ(p2roundup((unsigned)0x101, (unsigned)0x100), 0x200U);
}

TEST(P2SignedTest, Align) {
    EXPECT_EQ(p2align(-5, 4), -8);
    EXPECT_EQ(p2align(-4, 4), -4);
    EXPECT_EQ(p2align(-3, 4), -4);
    EXPECT_EQ(p2align(-1, 4), -4);
}

TEST(P2SignedTest, RoundUp) {
    EXPECT_EQ(p2roundup(-5, 4), -4);
    EXPECT_EQ(p2roundup(-4, 4), -4);
    EXPECT_EQ(p2roundup(-3, 4), 0);
    EXPECT_EQ(p2roundup(-1, 4), 0);
}

TEST(P2SignedTest, Phase) {
    EXPECT_EQ(p2phase(-5, 4), 3);
    EXPECT_EQ(p2phase(-4, 4), 0);
    EXPECT_EQ(p2phase(-3, 4), 1);
    EXPECT_EQ(p2phase(-1, 4), 3);
}

TEST(P2SignedTest, NPhase) {
    EXPECT_EQ(p2nphase(-5, 4), 1);
    EXPECT_EQ(p2nphase(-4, 4), 0);
    EXPECT_EQ(p2nphase(-1, 4), 1);
    EXPECT_EQ(p2nphase(0, 4), 0);
    EXPECT_EQ(p2nphase(1, 4), 3);
}

TEST(CtzTest, Zero) {
    EXPECT_EQ(ctz((uint8_t)0), 8U);
    EXPECT_EQ(ctz((uint16_t)0), 16U);
    EXPECT_EQ(ctz((uint32_t)0), 32U);
    EXPECT_EQ(ctz((uint64_t)0), 64U);
}

TEST(CtzTest, Values) {
    EXPECT_EQ(ctz((uint8_t)1), 0U);
    EXPECT_EQ(ctz((uint16_t)2), 1U);
    EXPECT_EQ(ctz((uint32_t)4), 2U);
    EXPECT_EQ(ctz((uint64_t)8), 3U);
    EXPECT_EQ(ctz((uint32_t)0x80000000), 31U);
    EXPECT_EQ(ctz((uint64_t)0x8000000000000000ULL), 63U);
}

TEST(ClzTest, Zero) {
    EXPECT_EQ(clz((uint8_t)0), 8U);
    EXPECT_EQ(clz((uint16_t)0), 16U);
    EXPECT_EQ(clz((uint32_t)0), 32U);
    EXPECT_EQ(clz((uint64_t)0), 64U);
}

TEST(ClzTest, Values) {
    EXPECT_EQ(clz((uint8_t)1), 7U);
    EXPECT_EQ(clz((uint16_t)1), 15U);
    EXPECT_EQ(clz((uint32_t)1), 31U);
    EXPECT_EQ(clz((uint64_t)1), 63U);
    EXPECT_EQ(clz((uint32_t)0x80000000), 0U);
    EXPECT_EQ(clz((uint64_t)0x8000000000000000ULL), 0U);
}

TEST(ClzTest, Various) {
    for (unsigned i = 0; i < 32; i++) {
        uint32_t v = 1U << i;
        EXPECT_EQ(clz(v), 31U - i);
    }
}

TEST(CBitsTest, Zero) {
    EXPECT_EQ(cbits((uint8_t)0), 0U);
    EXPECT_EQ(cbits((uint16_t)0), 0U);
    EXPECT_EQ(cbits((uint32_t)0), 0U);
    EXPECT_EQ(cbits((uint64_t)0), 0U);
}

TEST(CBitsTest, Values) {
    EXPECT_EQ(cbits((uint8_t)1), 1U);
    EXPECT_EQ(cbits((uint16_t)0xFF), 8U);
    EXPECT_EQ(cbits((uint32_t)0xFFFFFFFF), 32U);
    EXPECT_EQ(cbits((uint64_t)0xFFFFFFFFFFFFFFFFULL), 64U);
    EXPECT_EQ(cbits((uint32_t)0x80000000), 32U);
    EXPECT_EQ(cbits((uint32_t)0x7FFFFFFF), 31U);
}

TEST(CBitsTest, Consistency) {
    EXPECT_EQ(cbits((uint32_t)1), 32U - clz((uint32_t)1));
    EXPECT_EQ(cbits((uint32_t)0xFF), 32U - clz((uint32_t)0xFF));
    EXPECT_EQ(cbits((uint32_t)0x80000000), 32U - clz((uint32_t)0x80000000));
    EXPECT_EQ(cbits((uint8_t)1), 8U - clz((uint8_t)1));
    EXPECT_EQ(cbits((uint8_t)0xFF), 8U - clz((uint8_t)0xFF));
}

TEST(PopcountTest, Zero) {
    EXPECT_EQ(popcount((uint32_t)0), 0U);
    EXPECT_EQ(popcount((uint64_t)0), 0U);
}

TEST(PopcountTest, Values) {
    EXPECT_EQ(popcount((uint32_t)1), 1U);
    EXPECT_EQ(popcount((uint32_t)0x80000000), 1U);
    EXPECT_EQ(popcount((uint32_t)0xFFFFFFFF), 32U);
    EXPECT_EQ(popcount((uint64_t)0xFFFFFFFFFFFFFFFFULL), 64U);
    EXPECT_EQ(popcount((uint32_t)0x55555555), 16U);
    EXPECT_EQ(popcount((uint32_t)0xAAAAAAAA), 16U);
}
