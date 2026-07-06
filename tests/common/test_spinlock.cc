#include <gtest/gtest.h>

#include <mutex>
#include <thread>
#include <vector>

#include "common/spinlock.h"

using TOPNSPC::spinlock;

TEST(SpinlockTest, BasicLockUnlock) {
    spinlock lock;
    lock.lock();
    lock.unlock();
}

TEST(SpinlockTest, LockGuard) {
    spinlock lock;
    {
        std::lock_guard<spinlock> guard(lock);
    }
}

TEST(SpinlockTest, MutualExclusion) {
    spinlock lock;
    int counter = 0;
    constexpr int per_thread = 10000;
    constexpr int num_threads = 4;

    auto worker = [&]() {
        for (int i = 0; i < per_thread; ++i) {
            std::lock_guard<spinlock> guard(lock);
            ++counter;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(worker);
    for (auto &t : threads)
        t.join();

    EXPECT_EQ(counter, per_thread * num_threads);
}

TEST(SpinlockTest, StressTest) {
    spinlock lock;
    long long counter = 0;
    constexpr int per_thread = 100000;
    constexpr int num_threads = 8;

    auto worker = [&]() {
        for (int i = 0; i < per_thread; ++i) {
            std::lock_guard<spinlock> guard(lock);
            ++counter;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(worker);
    for (auto &t : threads)
        t.join();

    EXPECT_EQ(counter, static_cast<long long>(per_thread) * num_threads);
}

TEST(SpinlockTest, TryLockSucceedsWhenUncontended) {
    spinlock lock;
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

TEST(SpinlockTest, TryLockFailsWhenLocked) {
    spinlock lock;
    lock.lock();
    EXPECT_FALSE(lock.try_lock());
    lock.unlock();
}

TEST(SpinlockTest, TryLockAfterUnlock) {
    spinlock lock;
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

TEST(SpinlockTest, SequentialLockUnlock) {
    spinlock lock;
    for (int i = 0; i < 1000; ++i) {
        lock.lock();
        lock.unlock();
    }
}
