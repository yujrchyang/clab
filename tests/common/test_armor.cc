#include <gtest/gtest.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "common/armor.h"

using namespace TOPNSPC;

TEST(ArmorTest, EmptyInput) {
    char buf[16] = {};
    int ret = armor(buf, buf + sizeof(buf), nullptr, nullptr);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 0);
    EXPECT_STREQ(buf, "");
}

TEST(ArmorTest, SingleByte) {
    char buf[16] = {};
    const char src[] = "f";
    int ret = armor(buf, buf + sizeof(buf), src, src + 1);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 4);
    EXPECT_STREQ(buf, "Zg==");
}

TEST(ArmorTest, TwoBytes) {
    char buf[16] = {};
    const char src[] = "fo";
    int ret = armor(buf, buf + sizeof(buf), src, src + 2);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 4);
    EXPECT_STREQ(buf, "Zm8=");
}

TEST(ArmorTest, ThreeBytes) {
    char buf[16] = {};
    const char src[] = "foo";
    int ret = armor(buf, buf + sizeof(buf), src, src + 3);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 4);
    EXPECT_STREQ(buf, "Zm9v");
}

TEST(ArmorTest, FourBytes) {
    char buf[16] = {};
    const char src[] = "foob";
    int ret = armor(buf, buf + sizeof(buf), src, src + 4);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 8);
    EXPECT_STREQ(buf, "Zm9vYg==");
}

TEST(ArmorTest, SixBytes) {
    char buf[16] = {};
    const char src[] = "foobar";
    int ret = armor(buf, buf + sizeof(buf), src, src + 6);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 8);
    EXPECT_STREQ(buf, "Zm9vYmFy");
}

TEST(ArmorTest, KnownTestVector) {
    char buf[32] = {};
    const char src[] = "123456789";
    int ret = armor(buf, buf + sizeof(buf), src, src + 9);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 12);
    EXPECT_STREQ(buf, "MTIzNDU2Nzg5");
}

TEST(ArmorTest, BinaryData) {
    char buf[16] = {};
    const uint8_t src[] = {0x14, 0xFB, 0x9C, 0x03, 0xD9, 0x7E};
    int ret = armor(buf, buf + sizeof(buf),
                    reinterpret_cast<const char *>(src),
                    reinterpret_cast<const char *>(src + 6));
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 8);
    EXPECT_STREQ(buf, "FPucA9l+");
}

TEST(ArmorTest, BufferTooSmall) {
    char buf[3] = {};
    const char src[] = "abc";
    int ret = armor(buf, buf + sizeof(buf), src, src + 3);
    EXPECT_EQ(ret, -ERANGE);
}

TEST(ArmorTest, ExactBuffer) {
    char buf[5] = {};
    const char src[] = "foo";
    int ret = armor(buf, buf + sizeof(buf), src, src + 3);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 4);
    EXPECT_STREQ(buf, "Zm9v");
}

TEST(ArmorLineBreakTest, NoLineBreak) {
    char buf[32] = {};
    const char src[] = "hello";
    int ret = armor_linebreak(buf, buf + sizeof(buf), src, src + 5, 0);
    ASSERT_GE(ret, 0);
    EXPECT_STREQ(buf, "aGVsbG8=");
}

TEST(ArmorLineBreakTest, LineBreakAfterWidth) {
    char buf[32] = {};
    const char src[] = "foobar";
    int ret = armor_linebreak(buf, buf + sizeof(buf), src, src + 6, 4);
    ASSERT_GE(ret, 0);
    EXPECT_STREQ(buf, "Zm9v\nYmFy\n");
}

TEST(ArmorLineBreakTest, MultipleLineBreaks) {
    char buf[64] = {};
    const char src[] = "123456789";
    int ret = armor_linebreak(buf, buf + sizeof(buf), src, src + 9, 8);
    ASSERT_GE(ret, 0);
    EXPECT_STREQ(buf, "MTIzNDU2\nNzg5");
}

TEST(ArmorLineBreakTest, ExactMultipleOfLineWidth) {
    char buf[32] = {};
    const char src[] = "foobar";
    int ret = armor_linebreak(buf, buf + sizeof(buf), src, src + 6, 8);
    ASSERT_GE(ret, 0);
    EXPECT_STREQ(buf, "Zm9vYmFy\n");
}

TEST(ArmorLineBreakTest, LineBreakBufferTooSmall) {
    char buf[5] = {};
    const char src[] = "foob";
    int ret = armor_linebreak(buf, buf + sizeof(buf), src, src + 4, 4);
    EXPECT_EQ(ret, -ERANGE);
}

TEST(UnarmorTest, EmptyInput) {
    char buf[16] = {};
    int ret = unarmor(buf, buf + sizeof(buf), nullptr, nullptr);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 0);
}

TEST(UnarmorTest, SingleByte) {
    char buf[16] = {};
    const char src[] = "Zg==";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 4);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 1);
    EXPECT_EQ(buf[0], 'f');
}

TEST(UnarmorTest, TwoBytes) {
    char buf[16] = {};
    const char src[] = "Zm8=";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 4);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 2);
    EXPECT_EQ(std::string(buf, 2), "fo");
}

TEST(UnarmorTest, ThreeBytes) {
    char buf[16] = {};
    const char src[] = "Zm9v";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 4);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 3);
    EXPECT_EQ(std::string(buf, 3), "foo");
}

TEST(UnarmorTest, KnownTestVector) {
    char buf[16] = {};
    const char src[] = "MTIzNDU2Nzg5";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 12);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 9);
    EXPECT_EQ(std::string(buf, 9), "123456789");
}

TEST(UnarmorTest, BinaryData) {
    char buf[8] = {};
    const char src[] = "FPucA9l+";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 8);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 6);
    const uint8_t expected[] = {0x14, 0xFB, 0x9C, 0x03, 0xD9, 0x7E};
    EXPECT_EQ(std::memcmp(buf, expected, 6), 0);
}

TEST(UnarmorTest, SkipNewlines) {
    char buf[16] = {};
    const char src[] = "Zm9v\nYmFy";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 9);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 6);
    EXPECT_EQ(std::string(buf, 6), "foobar");
}

TEST(UnarmorTest, LeadingNewline) {
    char buf[16] = {};
    const char src[] = "\nZm9v";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 5);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 3);
    EXPECT_EQ(std::string(buf, 3), "foo");
}

TEST(UnarmorTest, MultipleNewlines) {
    char buf[16] = {};
    const char src[] = "Zm9v\nYmFy";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 9);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 6);
    EXPECT_EQ(std::string(buf, 6), "foobar");
}

TEST(UnarmorTest, BufferTooSmall) {
    char buf[2] = {};
    const char src[] = "Zm9v";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 4);
    EXPECT_EQ(ret, -ERANGE);
}

TEST(UnarmorTest, InvalidCharacter) {
    char buf[16] = {};
    const char src[] = "Zm!v";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 4);
    EXPECT_EQ(ret, -EINVAL);
}

TEST(UnarmorTest, TruncatedInput) {
    char buf[16] = {};
    const char src[] = "Zm9";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 3);
    EXPECT_EQ(ret, -EINVAL);
}

TEST(UnarmorTest, SingleCharInput) {
    char buf[16] = {};
    const char src[] = "Z";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 1);
    EXPECT_EQ(ret, -EINVAL);
}

TEST(UnarmorTest, AllWhitespace) {
    char buf[16] = {};
    const char src[] = "\n\n\n";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 3);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 0);
}

TEST(ArmorRoundTrip, SingleByte) {
    char enc[16] = {};
    char dec[16] = {};
    const char src[] = "x";
    int elen = armor(enc, enc + sizeof(enc), src, src + 1);
    ASSERT_GE(elen, 0);
    int dlen = unarmor(dec, dec + sizeof(dec), enc, enc + elen);
    ASSERT_GE(dlen, 0);
    EXPECT_EQ(dlen, 1);
    EXPECT_EQ(dec[0], 'x');
}

TEST(ArmorRoundTrip, AllBytes) {
    for (int len = 0; len <= 16; len++) {
        std::vector<char> src(len);
        for (int i = 0; i < len; i++)
            src[i] = static_cast<char>((i * 17 + 31) & 0xFF);

        std::vector<char> enc(len * 4 / 3 + 8, '\0');
        int elen = armor(enc.data(), enc.data() + enc.size(),
                         src.data(), src.data() + src.size());
        ASSERT_GE(elen, 0);

        std::vector<char> dec(len + 4, '\0');
        int dlen = unarmor(dec.data(), dec.data() + dec.size(),
                           enc.data(), enc.data() + elen);
        ASSERT_GE(dlen, 0);
        ASSERT_EQ(dlen, len);
        EXPECT_EQ(std::memcmp(src.data(), dec.data(), len), 0);
    }
}

TEST(ArmorRoundTrip, WithLineBreak) {
    char enc[64] = {};
    char dec[32] = {};
    const char src[] = "hello world";
    int elen = armor_linebreak(enc, enc + sizeof(enc),
                               src, src + 11, 4);
    ASSERT_GE(elen, 0);

    int dlen = unarmor(dec, dec + sizeof(dec), enc, enc + elen);
    ASSERT_GE(dlen, 0);
    EXPECT_EQ(dlen, 11);
    EXPECT_EQ(std::string(dec, 11), "hello world");
}

TEST(UnarmorTest, AlternatePlusMinus) {
    char buf[8] = {};
    const char src[] = "FPucA9l-";
    int ret = unarmor(buf, buf + sizeof(buf), src, src + 8);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 6);
    const uint8_t expected[] = {0x14, 0xFB, 0x9C, 0x03, 0xD9, 0x7E};
    EXPECT_EQ(std::memcmp(buf, expected, 6), 0);
}

TEST(UnarmorTest, AlternateSlashUnderscore) {
    const uint8_t raw[] = {0x3F, 0x3F, 0x3F, 0xF1};
    char standard[16] = {};
    int elen = armor(standard, standard + sizeof(standard),
                     reinterpret_cast<const char *>(raw),
                     reinterpret_cast<const char *>(raw + 4));
    ASSERT_GE(elen, 0);

    char modified[16] = {};
    std::memcpy(modified, standard, elen);
    for (int i = 0; i < elen; i++)
        if (modified[i] == '/') modified[i] = '_';

    char dec[8] = {};
    int ret = unarmor(dec, dec + sizeof(dec), modified, modified + elen);
    ASSERT_GE(ret, 0);
    EXPECT_EQ(ret, 4);
    EXPECT_EQ(std::memcmp(dec, raw, 4), 0);
}
