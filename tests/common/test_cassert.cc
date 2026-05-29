#include <gtest/gtest.h>

#include "common/cassert.h"

// Output goes to a /var/log/coredump+*.log file, not stderr, so match ".*".
#define EXPECT_DIES(statement) EXPECT_DEATH(statement, ".*")

TEST(common_cassert, assert_warn_passes) {
    int x = 1;
    EXPECT_NO_FATAL_FAILURE(common_assert(x == 1));
    EXPECT_NO_FATAL_FAILURE(common_assertf(x == 1, "x should be 1"));
    EXPECT_NO_FATAL_FAILURE(assert_warn(x == 1));
}

TEST(common_cassert, assert_fail_aborts) {
    EXPECT_DIES(common_assert(false));
}

TEST(common_cassert, assertf_fail_aborts) {
    EXPECT_DIES(common_assertf(false, "detailed reason"));
}

TEST(common_cassert, abort_msg) {
    EXPECT_DIES(common_abort_msg("oops"));
}

TEST(common_cassert, abortf) {
    EXPECT_DIES(common_abort("fatal %d", 42));
}

TEST(common_cassert, assert_warn_false) {
    EXPECT_NO_FATAL_FAILURE(assert_warn(false));
}
