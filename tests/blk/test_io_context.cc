#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <list>
#include <thread>

#include "blk/io_context.h"

TEST(IOContextTest, ConstructorSetsPriv) {
    int value = 42;
    IOContext ctx(&value);
    EXPECT_EQ(ctx.priv, &value);
}

TEST(IOContextTest, ConstructorPrivNull) {
    IOContext ctx(nullptr);
    EXPECT_EQ(ctx.priv, nullptr);
}

TEST(IOContextTest, ReturnValueRoundtrip) {
    IOContext ctx(nullptr);
    EXPECT_EQ(ctx.get_return_value(), 0);
    ctx.set_return_value(-EIO);
    EXPECT_EQ(ctx.get_return_value(), -EIO);
    ctx.set_return_value(0);
    EXPECT_EQ(ctx.get_return_value(), 0);
}

TEST(IOContextTest, DefaultReturnValue) {
    IOContext ctx(nullptr);
    EXPECT_EQ(ctx.get_return_value(), 0);
}

TEST(IOContextTest, HasPendingAiosInitiallyFalse) {
    IOContext ctx(nullptr);
    EXPECT_FALSE(ctx.has_pending_aios());
}

TEST(IOContextTest, HasPendingAiosAfterIncrement) {
    IOContext ctx(nullptr);
    ctx.num_pending.store(3);
    EXPECT_TRUE(ctx.has_pending_aios());
    ctx.num_pending.store(0);
    EXPECT_FALSE(ctx.has_pending_aios());
}

TEST(IOContextTest, GetNumIosInitiallyZero) {
    IOContext ctx(nullptr);
    EXPECT_EQ(ctx.get_num_ios(), 0);
}

TEST(IOContextTest, GetNumIosCountsPendingAios) {
    int marker = 0;
    int fd = -1;
    IOContext ctx(nullptr);
    ctx.pending_aios.emplace_back(&marker, fd);
    ctx.num_pending.fetch_add(1);
    EXPECT_EQ(ctx.get_num_ios(), 1);
    ctx.pending_aios.emplace_back(&marker, fd);
    ctx.pending_aios.emplace_back(&marker, fd);
    ctx.num_pending.fetch_add(2);
    EXPECT_EQ(ctx.get_num_ios(), 3);
}

TEST(IOContextTest, ReleaseRunningAiosClearsList) {
    int marker = 0;
    int fd = -1;
    IOContext ctx(nullptr);
    ctx.running_aios.emplace_back(&marker, fd);
    ctx.running_aios.emplace_back(&marker, fd);
    EXPECT_EQ(ctx.running_aios.size(), 2);
    ctx.release_running_aios();
    EXPECT_TRUE(ctx.running_aios.empty());
}

TEST(IOContextTest, ReleaseRunningAiosEmptyIsSafe) {
    IOContext ctx(nullptr);
    EXPECT_NO_THROW(ctx.release_running_aios());
}

TEST(IOContextTest, TryAioWakeDecrementsRunning) {
    IOContext ctx(nullptr);
    ctx.num_running.store(3);
    ctx.try_aio_wake();
    EXPECT_EQ(ctx.num_running.load(), 2);
    ctx.try_aio_wake();
    EXPECT_EQ(ctx.num_running.load(), 1);
    ctx.try_aio_wake();
    EXPECT_EQ(ctx.num_running.load(), 0);
}

TEST(IOContextTest, AioWaitWithZeroRunningReturnsImmediately) {
    IOContext ctx(nullptr);
    ctx.num_running.store(0);
    ctx.aio_wait();
}

TEST(IOContextTest, AioWaitBlocksThenWakes) {
    IOContext ctx(nullptr);
    ctx.num_running.store(1);

    std::thread waiter([&]() { ctx.aio_wait(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(ctx.num_running.load(), 1);

    ctx.try_aio_wake();
    waiter.join();
    EXPECT_EQ(ctx.num_running.load(), 0);
}

TEST(IOContextTest, MultipleTryAioWakeOnlyWakesOnLast) {
    IOContext ctx(nullptr);
    ctx.num_running.store(3);
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};

    std::thread waiter([&]() {
        started.store(true);
        ctx.aio_wait();
        done.store(true);
    });

    while (!started.load())
        std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    ctx.try_aio_wake();
    EXPECT_EQ(ctx.num_running.load(), 2);
    EXPECT_FALSE(done.load());

    ctx.try_aio_wake();
    EXPECT_EQ(ctx.num_running.load(), 1);
    EXPECT_FALSE(done.load());

    ctx.try_aio_wake();
    EXPECT_EQ(ctx.num_running.load(), 0);
    waiter.join();
    EXPECT_TRUE(done.load());
}

TEST(IOContextTest, ConcurrentTryAioWakeFromMultipleThreads) {
    constexpr int kNumIOs = 8;
    IOContext ctx(nullptr);
    ctx.num_running.store(kNumIOs);

    std::thread waiter([&]() { ctx.aio_wait(); });

    std::vector<std::thread> completers;
    for (int i = 0; i < kNumIOs; ++i)
        completers.emplace_back([&]() { ctx.try_aio_wake(); });

    for (auto &t : completers)
        t.join();
    waiter.join();
    EXPECT_EQ(ctx.num_running.load(), 0);
}

TEST(IOContextTest, LifecyclePendingToRunningToComplete) {
    int marker = 0;
    int fd = -1;
    IOContext ctx(nullptr);

    ctx.pending_aios.emplace_back(&marker, fd);
    ctx.pending_aios.emplace_back(&marker, fd);
    ctx.pending_aios.emplace_back(&marker, fd);
    ctx.num_pending.store(3);
    EXPECT_EQ(ctx.get_num_ios(), 3);
    EXPECT_TRUE(ctx.has_pending_aios());

    ctx.running_aios.splice(ctx.running_aios.end(), ctx.pending_aios);
    ctx.num_running.store(ctx.num_pending.load());
    ctx.num_pending.store(0);

    EXPECT_TRUE(ctx.pending_aios.empty());
    EXPECT_EQ(ctx.running_aios.size(), 3);
    EXPECT_EQ(ctx.num_running.load(), 3);
    EXPECT_FALSE(ctx.has_pending_aios());

    for (int i = 0; i < 3; ++i)
        ctx.try_aio_wake();
    EXPECT_EQ(ctx.num_running.load(), 0);

    ctx.release_running_aios();
    EXPECT_TRUE(ctx.running_aios.empty());
}

TEST(IOContextTest, NoCopy) {
    EXPECT_FALSE(std::is_copy_constructible<IOContext>::value);
    EXPECT_FALSE(std::is_copy_assignable<IOContext>::value);
}
