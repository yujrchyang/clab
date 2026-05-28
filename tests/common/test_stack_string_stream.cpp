#include <gtest/gtest.h>

#include <iomanip>
#include <sstream>
#include <string>

#include "common/stack_string_stream.hpp"

TEST(StackStringBufTest, SmallWriteWithinStackSize) {
    StackStringBuf<64> buf;
    std::string_view sv = buf.strv();
    EXPECT_TRUE(sv.empty());

    std::streamsize n = buf.sputn("hello", 5);
    EXPECT_EQ(n, 5);
    sv = buf.strv();
    EXPECT_EQ(sv, "hello");
}

TEST(StackStringBufTest, LargeWriteExceedsStackSize) {
    StackStringBuf<16> buf;
    std::string big(100, 'x');
    std::streamsize n = buf.sputn(big.data(), big.size());
    EXPECT_EQ(n, 100);
    std::string_view sv = buf.strv();
    EXPECT_EQ(sv.size(), 100);
    EXPECT_EQ(sv, big);
}

TEST(StackStringBufTest, OverflowSingleChar) {
    StackStringBuf<8> buf;
    buf.sputn("abc", 3);
    buf.sputn("def", 3);
    int rc = buf.sputc('!');
    EXPECT_NE(rc, EOF);
    EXPECT_EQ(buf.strv(), "abcdef!");
}

TEST(StackStringBufTest, OverflowAfterExactFit) {
    StackStringBuf<8> buf;
    buf.sputn("12345678", 8);
    buf.sputn("XX", 2);
    EXPECT_EQ(buf.strv(), "12345678XX");
}

TEST(StackStringBufTest, ClearReuse) {
    StackStringBuf<32> buf;
    buf.sputn("first", 5);
    buf.clear();
    EXPECT_TRUE(buf.strv().empty());
    buf.sputn("second", 6);
    EXPECT_EQ(buf.strv(), "second");
}

TEST(StackStringBufTest, StrvEmptyWhenEmpty) {
    StackStringBuf<64> buf;
    auto s = buf.strv();
    EXPECT_EQ(s.size(), 0u);
}

TEST(StackStringBufTest, MultipleSputn) {
    StackStringBuf<16> buf;
    buf.sputn("a", 1);
    buf.sputn("b", 1);
    buf.sputn("c", 1);
    EXPECT_EQ(buf.strv(), "abc");
}

class StackStringStreamTest : public ::testing::Test {
protected:
    StackStringStream<64> stream;
};

TEST_F(StackStringStreamTest, WriteAndStr) {
    stream << "hello " << 42;
    EXPECT_EQ(stream.str(), "hello 42");
}

TEST_F(StackStringStreamTest, Strv) {
    stream << "test";
    EXPECT_EQ(stream.strv(), "test");
}

TEST_F(StackStringStreamTest, ResetClearsContent) {
    stream << "data";
    EXPECT_FALSE(stream.strv().empty());
    stream.reset();
    EXPECT_TRUE(stream.strv().empty());
}

TEST_F(StackStringStreamTest, ResetRestoresFormatFlags) {
    stream << std::hex << 255;
    EXPECT_EQ(stream.str(), "ff");
    stream.reset();
    stream << 255;
    EXPECT_EQ(stream.str(), "255");
}

TEST_F(StackStringStreamTest, LargeContentBeyondStack) {
    std::string big(5000, 'y');
    stream << big;
    EXPECT_EQ(stream.str(), big);
}

TEST_F(StackStringStreamTest, MultipleWrites) {
    stream << "part1";
    stream << " part2";
    stream << " part3";
    EXPECT_EQ(stream.str(), "part1 part2 part3");
}

TEST_F(StackStringStreamTest, Manipulators) {
    stream << std::setw(6) << std::setfill('0') << 42;
    EXPECT_EQ(stream.str(), "000042");
}

TEST(CachedStackStringStreamTest, BasicUsage) {
    CachedStackStringStream css;
    *css << "hello " << 123;
    EXPECT_EQ(css->str(), "hello 123");
}

TEST(CachedStackStringStreamTest, ReuseAcrossScope) {
    std::string first;
    {
        CachedStackStringStream css;
        *css << "alpha";
        first = css->str();
    }
    {
        CachedStackStringStream css;
        *css << "beta";
        EXPECT_EQ(css->str(), "beta");
    }
    EXPECT_EQ(first, "alpha");
}

TEST(CachedStackStringStreamTest, ResetOnReuse) {
    std::string first_content;
    {
        CachedStackStringStream css;
        *css << "old data";
        first_content = css->str();
    }
    {
        CachedStackStringStream css;
        *css << "new data";
        std::string s = css->str();
        EXPECT_EQ(s, "new data");
    }
}

TEST(CachedStackStringStreamTest, Strv) {
    CachedStackStringStream css;
    *css << "view";
    EXPECT_EQ(css->strv(), "view");
}

TEST(CachedStackStringStreamTest, MultipleInstances) {
    CachedStackStringStream css1;
    CachedStackStringStream css2;
    *css1 << "one";
    *css2 << "two";
    EXPECT_EQ(css1->str(), "one");
    EXPECT_EQ(css2->str(), "two");
}

TEST(CachedStackStringStreamTest, GetPointer) {
    CachedStackStringStream css;
    EXPECT_NE(css.get(), nullptr);
    EXPECT_NE(css.operator->(), nullptr);
}
