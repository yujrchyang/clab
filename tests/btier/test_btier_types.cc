#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "btier/btier_types.h"
#include "common/buffer.h"
#include "common/crc32.h"

using namespace TOPNSPC;
using namespace TOPNSPC::btier;

// ── ExtentHeader size and layout ───────────────────────────────

TEST(BtierTypesTest, ExtentHeaderSize) {
    EXPECT_EQ(sizeof(ExtentHeader), 4096u);
}

TEST(BtierTypesTest, ExtentHeaderCrcOffset) {
    EXPECT_EQ(offsetof(ExtentHeader, crc), 40u);
}

TEST(BtierTypesTest, ExtentHeaderMagic) {
    EXPECT_EQ(ExtentHeader::MAGIC, 0x4254494552535445ULL);
}

TEST(BtierTypesTest, ExtentHeaderCrcCoversFirst40Bytes) {
    ExtentHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    hdr.magic = ExtentHeader::MAGIC;
    hdr.extent_id = 42;
    hdr.length = 4096;
    hdr.used_bytes = 100;
    hdr.live_bytes = 80;
    hdr.reserved = 0;
    hdr.generation = 5;

    uint32_t crc = calc_crc32(
        reinterpret_cast<const uint8_t *>(&hdr),
        offsetof(ExtentHeader, crc), 0);
    hdr.crc = crc;

    // Verify CRC matches
    uint32_t verify = calc_crc32(
        reinterpret_cast<const uint8_t *>(&hdr),
        offsetof(ExtentHeader, crc), 0);
    EXPECT_EQ(crc, verify);

    // Corrupt a field — CRC should differ
    hdr.used_bytes = 200;
    uint32_t bad_crc = calc_crc32(
        reinterpret_cast<const uint8_t *>(&hdr),
        offsetof(ExtentHeader, crc), 0);
    EXPECT_NE(crc, bad_crc);
}

// ── ExtentMetrics pack/unpack ──────────────────────────────────

TEST(BtierTypesTest, ExtentMetricsPackRoundtrip) {
    uint32_t access = 1234;
    uint32_t write = 567;
    uint32_t random = 42;
    uint32_t state = ACTIVE;
    uint64_t gen = 999;

    uint64_t packed = ExtentMetrics::pack(access, write, random, state, gen);

    EXPECT_EQ(ExtentMetrics::access_count(packed), access);
    EXPECT_EQ(ExtentMetrics::write_count(packed), write);
    EXPECT_EQ(ExtentMetrics::randomness(packed), random);
    EXPECT_EQ(ExtentMetrics::state(packed), state);
    EXPECT_EQ(ExtentMetrics::generation(packed), gen);
}

TEST(BtierTypesTest, ExtentMetricsMaxValues) {
    uint64_t packed = ExtentMetrics::pack(4095, 4095, 63, MIGRATING,
                                          0xFFFFFFFF);
    EXPECT_EQ(ExtentMetrics::access_count(packed), 4095u);
    EXPECT_EQ(ExtentMetrics::write_count(packed), 4095u);
    EXPECT_EQ(ExtentMetrics::randomness(packed), 63u);
    EXPECT_EQ(ExtentMetrics::state(packed), (uint32_t)MIGRATING);
    EXPECT_EQ(ExtentMetrics::generation(packed), 0xFFFFFFFFu);
}

TEST(BtierTypesTest, ExtentMetricsIsMigrating) {
    uint64_t active = ExtentMetrics::pack(0, 0, 0, ACTIVE, 0);
    uint64_t migrating = ExtentMetrics::pack(0, 0, 0, MIGRATING, 0);
    EXPECT_FALSE(ExtentMetrics::is_migrating(active));
    EXPECT_TRUE(ExtentMetrics::is_migrating(migrating));
}

TEST(BtierTypesTest, ExtentMetricsGenerationBump) {
    uint64_t packed = ExtentMetrics::pack(100, 50, 10, ACTIVE, 5);
    uint64_t new_gen = ExtentMetrics::generation(packed) + 1;
    uint64_t bumped = ExtentMetrics::pack(
        ExtentMetrics::access_count(packed),
        ExtentMetrics::write_count(packed),
        ExtentMetrics::randomness(packed),
        ExtentMetrics::state(packed),
        new_gen);
    EXPECT_EQ(ExtentMetrics::generation(bumped), 6u);
    // Other fields unchanged
    EXPECT_EQ(ExtentMetrics::access_count(bumped), 100u);
    EXPECT_EQ(ExtentMetrics::write_count(bumped), 50u);
}

// ── DENC roundtrip for DiskLocation ────────────────────────────

TEST(BtierTypesTest, DiskLocationDencRoundtrip) {
    DiskLocation orig;
    orig.offset = 0x1000;
    orig.length = 0x400000;
    orig.tier = Tier::SLOW;

    bufferlist bl;
    encode(orig, bl);

    DiskLocation decoded;
    auto p = bl.cbegin();
    decode(decoded, p);

    EXPECT_EQ(decoded.offset, orig.offset);
    EXPECT_EQ(decoded.length, orig.length);
    EXPECT_EQ(decoded.tier, orig.tier);
}

TEST(BtierTypesTest, DiskLocationDefaultValues) {
    DiskLocation dl;
    EXPECT_EQ(dl.offset, 0u);
    EXPECT_EQ(dl.length, 0u);
    EXPECT_EQ(dl.tier, Tier::FAST);
}

// ── DENC roundtrip for KeyLocation ──────────────────────────────

TEST(BtierTypesTest, KeyLocationDencRoundtrip) {
    KeyLocation orig;
    orig.extent_id = 12345;
    orig.offset = 678;
    orig.length = 4096;

    bufferlist bl;
    encode(orig, bl);

    KeyLocation decoded;
    auto p = bl.cbegin();
    decode(decoded, p);

    EXPECT_EQ(decoded.extent_id, orig.extent_id);
    EXPECT_EQ(decoded.offset, orig.offset);
    EXPECT_EQ(decoded.length, orig.length);
}

// ── Tier enum ──────────────────────────────────────────────────

TEST(BtierTypesTest, TierValues) {
    EXPECT_EQ(static_cast<uint8_t>(Tier::FAST), 0);
    EXPECT_EQ(static_cast<uint8_t>(Tier::SLOW), 1);
}
