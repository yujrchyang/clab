#include <gtest/gtest.h>

#include "common/valgrind.h"

namespace {
int dummy;
int array[8];
}

TEST(ValgrindTest, AnnotateHappensAfter) {
    ANNOTATE_HAPPENS_AFTER(&dummy);
    ANNOTATE_HAPPENS_AFTER(nullptr);
}

TEST(ValgrindTest, AnnotateHappensBefore) {
    ANNOTATE_HAPPENS_BEFORE(&dummy);
    ANNOTATE_HAPPENS_BEFORE(nullptr);
}

TEST(ValgrindTest, AnnotateHappensBeforeForgetAll) {
    ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(&dummy);
    ANNOTATE_HAPPENS_BEFORE_FORGET_ALL(nullptr);
}

TEST(ValgrindTest, AnnotateBenignRaceSized) {
    ANNOTATE_BENIGN_RACE_SIZED(&dummy, sizeof(dummy), "test benign race");
    ANNOTATE_BENIGN_RACE_SIZED(array, sizeof(array), "array race");
}

TEST(ValgrindTest, AllMacrosDefined) {
    EXPECT_TRUE(true);
}
