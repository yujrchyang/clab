#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "common/scope_guard.h"

using TOPNSPC::make_scope_guard;
using TOPNSPC::scope_guard;

TEST(ScopeGuardTest, FiresOnDestruction) {
    bool fired = false;
    {
        auto guard = scope_guard([&] { fired = true; });
    }
    EXPECT_TRUE(fired);
}

TEST(ScopeGuardTest, DismissPreventsFire) {
    bool fired = false;
    {
        auto guard = scope_guard([&] { fired = true; });
        guard.dismiss();
    }
    EXPECT_FALSE(fired);
}

TEST(ScopeGuardTest, MoveTransfersOwnership) {
    bool fired = false;
    {
        auto g1 = scope_guard([&] { fired = true; });
        auto g2 = std::move(g1);
        EXPECT_FALSE(fired);
    }
    EXPECT_TRUE(fired);
}

TEST(ScopeGuardTest, MoveSourceDoesNotFire) {
    int count = 0;
    {
        auto g1 = scope_guard([&] { ++count; });
        auto g2 = std::move(g1);
        (void)g2;
    }
    EXPECT_EQ(count, 1);
}

TEST(ScopeGuardTest, MakeScopeGuard) {
    bool fired = false;
    {
        auto guard = make_scope_guard([&] { fired = true; });
    }
    EXPECT_TRUE(fired);
}

TEST(ScopeGuardTest, DismissedAfterMoveSource) {
    int count = 0;
    {
        auto g1 = make_scope_guard([&] { ++count; });
        auto g2 = std::move(g1);
        g2.dismiss();
    }
    EXPECT_EQ(count, 0);
}

TEST(ScopeGuardTest, MultipleGuardsFireInReverseOrder) {
    std::vector<int> order;
    {
        auto g1 = make_scope_guard([&] { order.push_back(1); });
        auto g2 = make_scope_guard([&] { order.push_back(2); });
        (void)g1;
        (void)g2;
    }
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 2);
    EXPECT_EQ(order[1], 1);
}

TEST(ScopeGuardTest, MoveAssignTransfersAndFiresOld) {
    int count = 0;
    {
        std::function<void()> fn1 = [&] { ++count; };
        std::function<void()> fn2 = [&] { ++count; };
        auto g1 = scope_guard(std::move(fn1));
        auto g2 = scope_guard(std::move(fn2));
        g2 = std::move(g1);
        EXPECT_EQ(count, 1);
    }
    EXPECT_EQ(count, 2);
}

TEST(ScopeGuardTest, LambdaWithMoveOnlyCapture) {
    auto p = std::make_unique<int>(42);
    bool fired = false;
    {
        auto guard = scope_guard(
            [p = std::move(p), &fired] { EXPECT_EQ(*p, 42); fired = true; });
    }
    EXPECT_TRUE(fired);
}

struct CountingFunctor {
    int &counter;
    void operator()() { ++counter; }
};

TEST(ScopeGuardTest, FunctorWithInPlace) {
    int count = 0;
    {
        auto guard = scope_guard<CountingFunctor>(
            std::in_place, std::ref(count));
    }
    EXPECT_EQ(count, 1);
}

TEST(ScopeGuardTest, MakeScopeGuardWithInPlaceType) {
    int count = 0;
    {
        auto guard = make_scope_guard(
            std::in_place_type<CountingFunctor>, std::ref(count));
    }
    EXPECT_EQ(count, 1);
}

TEST(ScopeGuardTest, SelfMoveAssignNoCrash) {
    auto guard = make_scope_guard([] {});
    guard = std::move(guard);
}
