#include <gtest/gtest.h>

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>

#include "common/deleter.h"

using namespace TOPNSPC;

// =========================================================
// deleter basics
// =========================================================

TEST(DeleterTest, DefaultEmpty) {
    deleter d;
    EXPECT_FALSE(d);
}

TEST(DeleterTest, MoveConstruction) {
    bool ran = false;
    deleter d1 = make_deleter([&] { ran = true; });
    EXPECT_TRUE(d1);
    deleter d2(std::move(d1));
    EXPECT_FALSE(d1);
    EXPECT_TRUE(d2);
}

TEST(DeleterTest, MoveAssignment) {
    bool ran1 = false, ran2 = false;
    deleter d1 = make_deleter([&] { ran1 = true; });
    deleter d2 = make_deleter([&] { ran2 = true; });
    d2 = std::move(d1);
    EXPECT_FALSE(d1);
    EXPECT_TRUE(d2);
}

// =========================================================
// make_free_deleter — raw pointer freed via std::free
// =========================================================

TEST(DeleterTest, FreeDeleterFreesMemory) {
    void *p = std::malloc(64);
    ASSERT_NE(p, nullptr);
    {
        deleter d = make_free_deleter(p);
        EXPECT_TRUE(d);
    }
}

TEST(DeleterTest, FreeDeleterNullPtr) {
    deleter d = make_free_deleter(nullptr);
    EXPECT_FALSE(d);
}

// =========================================================
// make_deleter — lambda is called on destruction
// =========================================================

TEST(DeleterTest, LambdaRunsOnDestroy) {
    bool ran = false;
    {
        deleter d = make_deleter([&] { ran = true; });
        EXPECT_TRUE(d);
    }
    EXPECT_TRUE(ran);
}

TEST(DeleterTest, LambdaWithNext) {
    bool ran1 = false, ran2 = false;
    {
        deleter d2 = make_deleter([&] { ran2 = true; });
        deleter d1 = make_deleter(std::move(d2), [&] { ran1 = true; });
        EXPECT_TRUE(d1);
    }
    EXPECT_TRUE(ran1);
    EXPECT_TRUE(ran2);
}

// =========================================================
// share — shared ownership
// =========================================================

TEST(DeleterTest, ShareExtendsLifetime) {
    bool ran = false;
    {
        deleter d1 = make_deleter([&] { ran = true; });
        {
            deleter d2 = d1.share();
            EXPECT_TRUE(d1);
            EXPECT_TRUE(d2);
        }
        EXPECT_FALSE(ran);
    }
    EXPECT_TRUE(ran);
}

TEST(DeleterTest, ShareEmpty) {
    deleter d;
    deleter s = d.share();
    EXPECT_FALSE(s);
}

TEST(DeleterTest, ShareRawObject) {
    void *p = std::malloc(64);
    ASSERT_NE(p, nullptr);
    {
        deleter d1 = make_free_deleter(p);
        deleter d2 = d1.share();
        EXPECT_TRUE(d1);
        EXPECT_TRUE(d2);
    }
}

TEST(DeleterTest, ShareMultiple) {
    int count = 0;
    {
        deleter d1 = make_deleter([&] { count++; });
        deleter d2 = d1.share();
        deleter d3 = d2.share();
        EXPECT_EQ(count, 0);
    }
    EXPECT_EQ(count, 1);
}

// =========================================================
// append — chain deleters
// =========================================================

TEST(DeleterTest, AppendChain) {
    int order = 0;
    {
        deleter d2 = make_deleter([&] { order = order * 10 + 2; });
        deleter d1 = make_deleter([&] { order = order * 10 + 1; });
        d1.append(std::move(d2));
        EXPECT_TRUE(d1);
        EXPECT_FALSE(d2);
    }
    EXPECT_EQ(order, 12);
}

TEST(DeleterTest, AppendThree) {
    int order = 0;
    {
        deleter d3 = make_deleter([&] { order = order * 10 + 3; });
        deleter d2 = make_deleter([&] { order = order * 10 + 2; });
        deleter d1 = make_deleter([&] { order = order * 10 + 1; });
        d2.append(std::move(d3));
        d1.append(std::move(d2));
        EXPECT_TRUE(d1);
    }
    EXPECT_EQ(order, 123);
}

TEST(DeleterTest, AppendEmptyIsNoOp) {
    bool ran = false;
    {
        deleter d1 = make_deleter([&] { ran = true; });
        deleter empty;
        d1.append(std::move(empty));
        EXPECT_TRUE(d1);
    }
    EXPECT_TRUE(ran);
}

TEST(DeleterTest, AppendToEmpty) {
    bool ran = false;
    {
        deleter d1;
        deleter d2 = make_deleter([&] { ran = true; });
        d1.append(std::move(d2));
        EXPECT_TRUE(d1);
    }
    EXPECT_TRUE(ran);
}

// =========================================================
// make_free_deleter with next
// =========================================================

TEST(DeleterTest, FreeDeleterWithNext) {
    bool ran = false;
    void *p = std::malloc(64);
    ASSERT_NE(p, nullptr);
    {
        deleter d = make_free_deleter(
            make_deleter([&] { ran = true; }), p);
        EXPECT_TRUE(d);
    }
    EXPECT_TRUE(ran);
}

// =========================================================
// make_object_deleter
// =========================================================

TEST(DeleterTest, ObjectDeleterHoldsObject) {
    bool ran = false;
    struct Foo {
        bool *flag;
        ~Foo() { *flag = true; }
    };
    {
        deleter d = make_object_deleter(Foo{&ran});
        EXPECT_TRUE(d);
    }
    EXPECT_TRUE(ran);
}

TEST(DeleterTest, ObjectDeleterWithNext) {
    int order = 0;
    struct Foo {
        int *p;
        Foo(int *p) : p(p) {}
        Foo(Foo &&o) : p(o.p) { o.p = nullptr; }
        ~Foo() {
            if (p) *p = *p * 10 + 2;
        }
    };
    {
        deleter d2 = make_deleter([&] { order = order * 10 + 1; });
        deleter d1 = make_object_deleter(std::move(d2), Foo{&order});
        EXPECT_TRUE(d1);
    }
    EXPECT_EQ(order, 21);
}

// =========================================================
// reset
// =========================================================

TEST(DeleterTest, ResetReplacesDeleter) {
    bool ran1 = false, ran2 = false;
    deleter d = make_deleter([&] { ran1 = true; });
    EXPECT_TRUE(d);
    auto *impl = new lambda_deleter_impl(
        deleter(), [&] { ran2 = true; });
    d.reset(impl);
    EXPECT_TRUE(d);
    EXPECT_TRUE(ran1);
}

// =========================================================
// move assignment replaces and destroys old
// =========================================================

TEST(DeleterTest, MoveAssignDestroysOld) {
    bool ran1 = false, ran2 = false;
    deleter d = make_deleter([&] { ran1 = true; });
    deleter d2 = make_deleter([&] { ran2 = true; });
    d = std::move(d2);
    EXPECT_TRUE(ran1);
}
