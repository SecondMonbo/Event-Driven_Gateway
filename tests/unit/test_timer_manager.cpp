#include <gtest/gtest.h>

#include "core/TimerManager.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
bool pump_until(TimerManager &tm,
                const std::function<bool()> &predicate,
                std::chrono::milliseconds timeout = std::chrono::seconds(2))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        tm.process_expired_timers();
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    tm.process_expired_timers();
    return predicate();
}
}

TEST(TimerManagerTest, OneShotTimerFires)
{
    TimerManager tm;
    std::atomic<int> calls{0};

    tm.add_timer(30, [&]() { ++calls; });
    ASSERT_TRUE(pump_until(tm, [&]() { return calls.load() == 1; }));
    EXPECT_EQ(calls.load(), 1);
}

TEST(TimerManagerTest, CancelPreventsCallback)
{
    TimerManager tm;
    std::atomic<int> calls{0};

    auto id = tm.add_timer(100, [&]() { ++calls; });
    ASSERT_TRUE(tm.cancel_timer(id));
    EXPECT_FALSE(tm.cancel_timer(id));

    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    EXPECT_EQ(calls.load(), 0);
}

TEST(TimerManagerTest, PeriodicTimerRunsAndCanBeCancelled)
{
    TimerManager tm;
    std::atomic<int> calls{0};

    auto id = tm.add_timer(10, [&]() { ++calls; }, 10);
    ASSERT_TRUE(pump_until(tm, [&]() { return calls.load() >= 3; }));

    ASSERT_TRUE(tm.cancel_timer(id));
    const int at_cancel = calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_EQ(calls.load(), at_cancel);
}

TEST(TimerManagerTest, CallbackCanAddAnotherTimer)
{
    TimerManager tm;
    std::atomic<int> second_calls{0};

    tm.add_timer(10, [&]()
                 {
                     tm.add_timer(10, [&]() { ++second_calls; });
                 });

    ASSERT_TRUE(pump_until(tm, [&]() { return second_calls.load() == 1; }));
    EXPECT_EQ(second_calls.load(), 1);
}

TEST(TimerManagerTest, CallbackCanCancelItself)
{
    TimerManager tm;
    std::atomic<int> calls{0};
    uint64_t id = 0;

    id = tm.add_timer(10, [&]()
                      {
                          ++calls;
                          tm.cancel_timer(id);
                      },
                      10);

    ASSERT_TRUE(pump_until(tm, [&]() { return calls.load() == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    EXPECT_EQ(calls.load(), 1);
}

TEST(TimerManagerTest, WakeupCallbackAndNextTimeout)
{
    TimerManager tm;
    std::atomic<int> wakeups{0};
    tm.set_wakeup_callback([&]() { ++wakeups; });

    EXPECT_EQ(tm.get_next_timeout_ms(), -1);

    tm.add_timer(50, []() {});
    EXPECT_GT(tm.get_next_timeout_ms(), 0);
    EXPECT_EQ(wakeups.load(), 1);
}

TEST(TimerManagerTest, CancelUnknownIdReturnsFalse)
{
    TimerManager tm;
    EXPECT_FALSE(tm.cancel_timer(12345));
}
