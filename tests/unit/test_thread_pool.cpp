#include <gtest/gtest.h>

#include "core/ThreadPool.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace
{
bool pump_until(ThreadPool &pool,
                const std::function<bool()> &predicate,
                std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        pool.process_completions();
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    pool.process_completions();
    return predicate();
}
}

TEST(ThreadPoolTest, RunsVoidTaskAndCallback)
{
    ThreadPool pool(2);
    std::atomic<bool> ran{false};
    std::atomic<bool> callback_ran{false};

    pool.enqueue_with_callback(
        [&]() { ran = true; },
        [&]() { callback_ran = true; });

    ASSERT_TRUE(pump_until(pool, [&]() { return callback_ran.load(); }));
    EXPECT_TRUE(ran.load());
    EXPECT_TRUE(callback_ran.load());
}

TEST(ThreadPoolTest, ReturnsValueToCallback)
{
    ThreadPool pool(2);
    std::string received;
    std::atomic<bool> done{false};

    pool.enqueue_with_callback(
        []() -> std::string { return "hello"; },
        [&](std::string value)
        {
            received = std::move(value);
            done = true;
        });

    ASSERT_TRUE(pump_until(pool, [&]() { return done.load(); }));
    EXPECT_EQ(received, "hello");
}

TEST(ThreadPoolTest, ReportsWorkerCount)
{
    ThreadPool pool(3);
    EXPECT_EQ(pool.worker_count(), 3u);
}

TEST(ThreadPoolTest, StopPreventsNewTasks)
{
    ThreadPool pool(1);
    pool.stop();
    EXPECT_THROW(
        pool.enqueue_with_callback([]() {}, []() {}),
        std::runtime_error);
}
