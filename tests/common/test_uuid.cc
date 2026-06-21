#include <gtest/gtest.h>

#include <cstdint>

#include "common/uuid.h"

using namespace TOPNSPC;

TEST(UuidD, DefaultZero) {
    uuid_d u;
    EXPECT_TRUE(u.is_zero());
}

TEST(UuidD, GenerateNonZero) {
    uuid_d u;
    u.generate();
    EXPECT_FALSE(u.is_zero());
}

TEST(UuidD, GenerateRfc4122Version4) {
    uuid_d u;
    u.generate();
    // version 4 → byte 6 bits 7-4 == 0100
    EXPECT_EQ(u.uuid[6] & 0xf0, 0x40);
    // variant (10xx) → byte 8 bits 7-6 == 10
    EXPECT_EQ(u.uuid[8] & 0xc0, 0x80);
}

TEST(UuidD, GenerateUnique) {
    uuid_d a, b;
    a.generate();
    b.generate();
    EXPECT_NE(a, b);
}

TEST(UuidD, Comparison) {
    uuid_d a, b;
    EXPECT_EQ(a, b);
    a.generate();
    EXPECT_NE(a, b);
}

TEST(UuidD, DencRoundtrip) {
    uuid_d u;
    u.generate();
    bufferlist bl;
    encode(u, bl);
    auto p = bl.cbegin();
    uuid_d v;
    decode(v, p);
    EXPECT_EQ(u, v);
}
