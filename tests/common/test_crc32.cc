#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "common/crc32.h"

using namespace TOPNSPC;

namespace TOPNSPC {
extern uint32_t crc32c_software_fallback(
    const uint8_t *data, size_t length, uint32_t previous_crc);
}

static uint32_t reference_crc32c(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0x82F63B78;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

TEST(Crc32SoftwareTest, StandardCheckVector) {
    const uint8_t data[] = "123456789";
    EXPECT_EQ(crc32c_software_fallback(data, 9, 0), 0xE3069283);
}

TEST(Crc32SoftwareTest, SingleByteA) {
    const uint8_t data[] = "a";
    EXPECT_EQ(crc32c_software_fallback(data, 1, 0), 0xC1D04330);
}

TEST(Crc32SoftwareTest, SingleNullByte) {
    const uint8_t data[] = {0x00};
    EXPECT_EQ(crc32c_software_fallback(data, 1, 0), 0x527D5351);
}

TEST(Crc32SoftwareTest, HelloWorld) {
    const uint8_t data[] = "hello world";
    EXPECT_EQ(crc32c_software_fallback(data, 11, 0), 0xC99465AA);
}

TEST(Crc32SoftwareTest, MatchesReference) {
    std::vector<uint8_t> buf(4096);
    for (size_t i = 0; i < buf.size(); i++) {
        buf[i] = static_cast<uint8_t>(i * 7 + 13);
    }
    EXPECT_EQ(crc32c_software_fallback(buf.data(), buf.size(), 0),
              reference_crc32c(buf.data(), buf.size()));
}

TEST(Crc32Test, StandardCheckVector) {
    const uint8_t data[] = "123456789";
    EXPECT_EQ(calc_crc32(data, 9, 0), 0xE3069283);
}

TEST(Crc32Test, SingleByteA) {
    const uint8_t data[] = "a";
    EXPECT_EQ(calc_crc32(data, 1, 0), 0xC1D04330);
}

TEST(Crc32Test, MatchesSoftwareFallback) {
    const uint8_t data[] = "123456789";
    EXPECT_EQ(calc_crc32(data, 9, 0),
              crc32c_software_fallback(data, 9, 0));
}

TEST(Crc32Test, MatchesSoftwareFallbackNonZeroSeed) {
    const uint8_t data[] = "hello world";
    EXPECT_EQ(calc_crc32(data, 11, 0xABCD),
              crc32c_software_fallback(data, 11, 0xABCD));
}

TEST(Crc32Test, MatchesSoftwareFallbackLargeBuffer) {
    std::vector<uint8_t> buf(4096);
    for (size_t i = 0; i < buf.size(); i++) {
        buf[i] = static_cast<uint8_t>(i * 7 + 13);
    }
    EXPECT_EQ(calc_crc32(buf.data(), buf.size(), 0),
              crc32c_software_fallback(buf.data(), buf.size(), 0));
}

TEST(Crc32Test, MatchesSoftwareFallbackIncremental) {
    std::vector<uint8_t> buf(4096);
    for (size_t i = 0; i < buf.size(); i++) {
        buf[i] = static_cast<uint8_t>(i * 3 + 7);
    }
    uint32_t c1_0 = calc_crc32(buf.data(), 1024, 0);
    uint32_t c1_1 = calc_crc32(buf.data() + 1024, 1024, c1_0);
    uint32_t c1_2 = calc_crc32(buf.data() + 2048, 1024, c1_1);
    uint32_t c1_3 = calc_crc32(buf.data() + 3072, 1024, c1_2);

    uint32_t c2_0 = crc32c_software_fallback(buf.data(), 1024, 0);
    uint32_t c2_1 = crc32c_software_fallback(buf.data() + 1024, 1024, c2_0);
    uint32_t c2_2 = crc32c_software_fallback(buf.data() + 2048, 1024, c2_1);
    uint32_t c2_3 = crc32c_software_fallback(buf.data() + 3072, 1024, c2_2);

    EXPECT_EQ(c1_3, c2_3);
}

TEST(Crc32Test, NullDataZeroLength) {
    EXPECT_EQ(calc_crc32(nullptr, 0, 0), 0U);
}

TEST(Crc32Test, NullDataNonZeroSeed) {
    EXPECT_EQ(calc_crc32(nullptr, 0, 0xDEADBEEF), 0xDEADBEEF);
}

TEST(Crc32Test, EmptyDataDefaultSeed) {
    uint8_t buf[] = {0xAB};
    EXPECT_EQ(calc_crc32(buf, 0, 42), 42U);
}

TEST(Crc32Test, DefaultSeed) {
    EXPECT_EQ(calc_crc32(nullptr, 0), 0U);
}

TEST(Crc32Test, IncrementalTwoChunks) {
    const uint8_t p1[] = "hello ";
    const uint8_t p2[] = "world";
    uint32_t h = calc_crc32(p1, 6);
    h = calc_crc32(p2, 5, h);
    const uint8_t whole[] = "hello world";
    EXPECT_EQ(h, calc_crc32(whole, 11));
}

TEST(Crc32Test, IncrementalThreeChunks) {
    const uint8_t a[] = "ab";
    const uint8_t b[] = "c";
    const uint8_t c[] = "def";
    uint32_t h = calc_crc32(a, 2);
    h = calc_crc32(b, 1, h);
    h = calc_crc32(c, 3, h);
    const uint8_t whole[] = "abcdef";
    EXPECT_EQ(h, calc_crc32(whole, 6));
}

TEST(Crc32Test, LargeBufferConsistency) {
    std::vector<uint8_t> buf(4096);
    for (size_t i = 0; i < buf.size(); i++) {
        buf[i] = static_cast<uint8_t>(i * 7 + 13);
    }
    uint32_t full = calc_crc32(buf.data(), buf.size());
    uint32_t h = calc_crc32(buf.data(), 1024);
    h = calc_crc32(buf.data() + 1024, 1024, h);
    h = calc_crc32(buf.data() + 2048, 1024, h);
    h = calc_crc32(buf.data() + 3072, 1024, h);
    EXPECT_EQ(h, full);
}

TEST(Crc32Test, DifferentSeedDifferentResult) {
    const uint8_t data[] = "hello";
    uint32_t r1 = calc_crc32(data, 5, 0);
    uint32_t r2 = calc_crc32(data, 5, 0xFFFFFFFF);
    EXPECT_NE(r1, r2);
}

TEST(Crc32Test, EmbeddedNull) {
    const uint8_t data[] = {'a', '\0', 'b'};
    uint32_t h1 = calc_crc32(data, 3);
    uint32_t h2 = calc_crc32(data, 1);
    h2 = calc_crc32(data + 1, 1, h2);
    h2 = calc_crc32(data + 2, 1, h2);
    EXPECT_EQ(h1, h2);
}
