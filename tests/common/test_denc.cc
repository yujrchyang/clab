#include <gtest/gtest.h>

#include "common/denc.h"

using namespace TOPNSPC;

namespace TOPNSPC {
struct test_foo {
    int32_t x;
    int64_t y;
    std::string s;
    DENC(test_foo, v, p) {
        DENC_START(1, 1, p);
        denc(v.x, p);
        denc(v.y, p);
        denc(v.s, p);
        DENC_FINISH(p);
    }
};
WRITE_CLASS_DENC(test_foo)
}  // namespace TOPNSPC

TEST(common_denc, encode_decode_roundtrip) {
    test_foo f{42, 99, "hello"};
    buffer::list bl;
    encode(f, bl);
    auto p = bl.cbegin();
    test_foo g{};
    decode(g, p);
    EXPECT_EQ(g.x, 42);
    EXPECT_EQ(g.y, 99);
    EXPECT_EQ(g.s, "hello");
}

TEST(common_denc, string_roundtrip) {
    std::string s = "hello denc";
    buffer::list bl;
    encode(s, bl);
    auto p = bl.cbegin();
    std::string t;
    decode(t, p);
    EXPECT_EQ(t, "hello denc");
}

TEST(common_denc, std_vector) {
    std::vector<int32_t> v{1, 2, 3, 4, 5};
    buffer::list bl;
    encode(v, bl);
    auto p = bl.cbegin();
    std::vector<int32_t> w;
    decode(w, p);
    EXPECT_EQ(w, v);
}

TEST(common_denc, int_roundtrip) {
    int32_t v = -42;
    buffer::list bl;
    encode(v, bl);
    auto p = bl.cbegin();
    int32_t w = 0;
    decode(w, p);
    EXPECT_EQ(w, -42);
}

TEST(common_denc, uint64_roundtrip) {
    uint64_t v = 0xDEADBEEFCAFE;
    buffer::list bl;
    encode(v, bl);
    auto p = bl.cbegin();
    uint64_t w = 0;
    decode(w, p);
    EXPECT_EQ(w, 0xDEADBEEFCAFE);
}

TEST(common_denc, bool_roundtrip) {
    for (bool b : {true, false}) {
        buffer::list bl;
        encode(b, bl);
        auto p = bl.cbegin();
        bool out = false;
        decode(out, p);
        EXPECT_EQ(out, b);
    }
}

TEST(common_denc, uint8_roundtrip) {
    uint8_t v = 0xAB;
    buffer::list bl;
    encode(v, bl);
    auto p = bl.cbegin();
    uint8_t w = 0;
    decode(w, p);
    EXPECT_EQ(w, 0xAB);
}

TEST(common_denc, empty_string) {
    std::string s;
    buffer::list bl;
    encode(s, bl);
    auto p = bl.cbegin();
    std::string t = "nonempty";
    decode(t, p);
    EXPECT_TRUE(t.empty());
}

TEST(common_denc, empty_vector) {
    std::vector<int32_t> v;
    buffer::list bl;
    encode(v, bl);
    auto p = bl.cbegin();
    std::vector<int32_t> w{99, 100};
    decode(w, p);
    EXPECT_TRUE(w.empty());
}

TEST(common_denc, std_map) {
    std::map<std::string, int32_t> m{{"a", 1}, {"b", 2}, {"c", 3}};
    buffer::list bl;
    encode(m, bl);
    auto p = bl.cbegin();
    std::map<std::string, int32_t> n;
    decode(n, p);
    EXPECT_EQ(n, m);
}

TEST(common_denc, std_set) {
    std::set<int32_t> s{3, 1, 4, 1, 5, 9};
    buffer::list bl;
    encode(s, bl);
    auto p = bl.cbegin();
    std::set<int32_t> t;
    decode(t, p);
    EXPECT_EQ(t, (std::set<int32_t>{1, 3, 4, 5, 9}));
}

TEST(common_denc, std_array) {
    std::array<int32_t, 4> a{10, 20, 30, 40};
    buffer::list bl;
    encode(a, bl);
    auto p = bl.cbegin();
    std::array<int32_t, 4> b{};
    decode(b, p);
    EXPECT_EQ(b, a);
}

TEST(common_denc, std_tuple) {
    auto t = std::make_tuple(42, std::string("hello"), int64_t(-99));
    buffer::list bl;
    encode(t, bl);
    auto p = bl.cbegin();
    decltype(t) u;
    decode(u, p);
    EXPECT_EQ(u, t);
}

TEST(common_denc, optional_empty) {
    std::optional<int32_t> o;
    buffer::list bl;
    encode(o, bl);
    auto p = bl.cbegin();
    std::optional<int32_t> q{42};
    decode(q, p);
    EXPECT_FALSE(q.has_value());
}

TEST(common_denc, optional_populated) {
    std::optional<int32_t> o{42};
    buffer::list bl;
    encode(o, bl);
    auto p = bl.cbegin();
    std::optional<int32_t> q;
    decode(q, p);
    ASSERT_TRUE(q.has_value());
    EXPECT_EQ(*q, 42);
}

TEST(common_denc, negative_signed) {
    int64_t v = -1234567890123LL;
    buffer::list bl;
    encode(v, bl);
    auto p = bl.cbegin();
    int64_t w = 0;
    decode(w, p);
    EXPECT_EQ(w, v);
}

TEST(common_denc, large_varint) {
    uint64_t v = 0xFFFFFFFFFFFFFFFFULL;
    buffer::list bl;
    encode(v, bl);
    auto p = bl.cbegin();
    uint64_t w = 0;
    decode(w, p);
    EXPECT_EQ(w, v);
}

TEST(common_denc, struct_with_empty_string) {
    test_foo f{1, 2, ""};
    buffer::list bl;
    encode(f, bl);
    auto p = bl.cbegin();
    test_foo g{};
    decode(g, p);
    EXPECT_EQ(g.x, 1);
    EXPECT_EQ(g.y, 2);
    EXPECT_TRUE(g.s.empty());
}

TEST(common_denc, struct_with_long_string) {
    std::string long_str(1000, 'X');
    test_foo f{7, 8, long_str};
    buffer::list bl;
    encode(f, bl);
    auto p = bl.cbegin();
    test_foo g{};
    decode(g, p);
    EXPECT_EQ(g.x, 7);
    EXPECT_EQ(g.y, 8);
    EXPECT_EQ(g.s, long_str);
}
