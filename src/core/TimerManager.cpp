#include "core/TimerManager.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

// 辅助函数，获取当前绝对时间（毫秒）
static uint64_t get_current_time_ms()
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    return duration_cast<milliseconds>(now.time_since_epoch()).count();
}

uint64_t TimerManager::add_timer(uint64_t delay_ms,
                                 std::function<void()> callback,
                                 uint64_t interval_ms)
{
    uint64_t id;
    bool need_wake = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        TimerNode node;
        node.id = next_id_++;
        node.expire_ms = get_current_time_ms() + delay_ms;
        node.interval_ms = interval_ms;
        node.callback = std::move(callback);

        need_wake = (timers_.empty() || node.expire_ms < timers_.top().expire_ms);

        id = node.id;
        active_ids_.insert(id);
        timers_.push(std::move(node));
    }
    if (need_wake && wakeup_fn_)
        wakeup_fn_(); // 锁外唤醒

    return id;
}

int64_t TimerManager::get_next_timeout_ms() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (timers_.empty())
        return -1;

    uint64_t now_ms = get_current_time_ms();
    uint64_t next_expire = timers_.top().expire_ms;

    if (next_expire <= now_ms)
        return 0;
    else
        return static_cast<int64_t>(next_expire - now_ms);
}

bool TimerManager::cancel_timer(uint64_t timer_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (active_ids_.erase(timer_id) == 0)
        return false;

    canceled_map_[timer_id] = true;
    return true;
}

void TimerManager::clean_canceled_timers()
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TimerNode> temp;
    while (!timers_.empty())
    {
        TimerNode node = std::move(const_cast<TimerNode &>(timers_.top()));
        timers_.pop();

        if (canceled_map_.find(node.id) == canceled_map_.end())
        {
            // 未取消
            temp.push_back(std::move(node));
        }
        else
        {
            canceled_map_.erase(node.id);
        }
    }

    for (auto &node : temp)
    {
        timers_.push(std::move(node));
    }
}

void TimerManager::set_wakeup_callback(std::function<void()> fn)
{
    wakeup_fn_ = std::move(fn);
}

void TimerManager::process_expired_timers()
{
    std::vector<TimerNode> due;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t now = get_current_time_ms();

        while (!timers_.empty() && timers_.top().expire_ms <= now)
        {
            TimerNode node = timers_.top();
            timers_.pop();

            if (canceled_map_.erase(node.id))
            {
                active_ids_.erase(node.id);
                continue;
            }

            due.push_back(std::move(node));
        }
    }

    for (auto &node : due)
    {
        {
            // 回调前检查：防止被之前的调用取消
            std::lock_guard<std::mutex> lock(mutex_);
            if (canceled_map_.erase(node.id))
            {
                active_ids_.erase(node.id);
                continue;
            }
        }

        try
        {
            if (node.callback)
                node.callback();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Timer callback exception: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Timer callback unknown exception" << std::endl;
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // 准备多次任务的入列
        if (canceled_map_.erase(node.id))
        {
            active_ids_.erase(node.id);
            continue;
        }

        if (node.interval_ms > 0)
        {
            node.expire_ms = get_current_time_ms() + node.interval_ms;
            timers_.push(std::move(node));
        }
        else
        {
            active_ids_.erase(node.id);
        }
    }
}