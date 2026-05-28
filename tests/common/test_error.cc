#include <gtest/gtest.h>

#include <cerrno>
#include <string>

#include "common/error.h"

using clab::cpp_strerror;

TEST(ErrorTest, KnownPositiveErrno) {
    std::string s = cpp_strerror(EINVAL);
    EXPECT_TRUE(s.starts_with("(22) "));
    EXPECT_FALSE(s.empty());
}

TEST(ErrorTest, NegativeErrno) {
    EXPECT_EQ(cpp_strerror(-EINVAL), cpp_strerror(EINVAL));
    EXPECT_EQ(cpp_strerror(-ENOENT), cpp_strerror(ENOENT));
}

TEST(ErrorTest, ErrnoZero) {
    std::string s = cpp_strerror(0);
    EXPECT_TRUE(s.starts_with("(0) "));
    EXPECT_FALSE(s.empty());
}

TEST(ErrorTest, InvalidErrno) {
    std::string s = cpp_strerror(999999);
    EXPECT_TRUE(s.starts_with("(999999) "));
    EXPECT_FALSE(s.empty());
}

TEST(ErrorTest, NegativeInvalid) {
    std::string s = cpp_strerror(-999999);
    EXPECT_TRUE(s.starts_with("(999999) "));
    EXPECT_FALSE(s.empty());
}

TEST(ErrorTest, MultipleErrnoValues) {
    EXPECT_EQ(cpp_strerror(ENOENT), cpp_strerror(2));
    EXPECT_EQ(cpp_strerror(EACCES), cpp_strerror(13));
}
