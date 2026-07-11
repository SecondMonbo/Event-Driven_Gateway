#include "TimerManager.hpp"
#include <chrono>
#include <algorithm>
#include <iostream>

// 辅助函数，获取当前绝对时间（毫秒）
static uint64_t get_current_time_ms()
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    return duration_cast<milliseconds>(now.time_since_epoch()).count();
}

uint64_t TimerManager::add_timer(uint64_t delay_ms, std::function<void()> callback, uint64_t interval_ms)
{
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t now_ms = get_current_time_ms();
    TimerNode node;
    node.id = next_id_++;
    node.expire_ms = now_ms + delay_ms;
    node.interval_ms = interval_ms;
    node.callback = std::move(callback);
    node.canceled = false;

    timers_.push(std::move(node));
    return node.id;
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

    if (canceled_map_.find(timer_id) != canceled_map_.end())
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

void TimerManager::process_expired_timers()
{
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t now_ms = get_current_time_ms();

    while (!timers_.empty())
    {
        const TimerNode &top = timers_.top();
        if (top.expire_ms > now_ms)
            break; // 没到时间

        // 取出任务
        TimerNode node = std::move(const_cast<TimerNode &>(timers_.top()));
        timers_.pop();

        // 检查取消标记
        auto it = canceled_map_.find(node.id);
        if (it != canceled_map_.end())
        {
            canceled_map_.end();
            canceled_map_.erase(it); // 移除
            continue;
        }

        // 执行回调
        if (node.callback)
        {
            try
            {
                node.callback();
            }
            catch (const std::exception &e)
            {
                std::cerr << "Timer callback expception: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "Timer callback unknown exception" << std::endl;
            }
        }

        // 如果周期性任务重新调度
        if (node.interval_ms > 0)
        {
            node.expire_ms = now_ms + node.interval_ms;
            node.canceled = false;
            timers_.push(std::move(node));
        }
    }
}