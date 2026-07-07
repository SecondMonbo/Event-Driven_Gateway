#pragma once

#include <functional>
#include <queue>
#include <vector>
#include <mutex>
#include <chrono>

struct TimerNode
{
    uint64_t id;
    uint64_t expire_ms;   // 绝对时间（毫秒）
    uint64_t interval_ms; // 0 表示一次性任务
    std::function<void()> callback;
    bool canceled = false;
};

// 比较器：改优先队列为最小堆（最早超时优先）
struct TimerCompare
{
    bool operator()(const TimerNode &a, const TimerNode &b) const
    {
        return a.expire_ms > b.expire_ms;
    }
};

class TimerManager
{
public:
    TimerManager() = default;
    ~TimerManager() = default;

    // 添加定时任务返回任务ID
    uint64_t add_timer(uint64_t delay_ms, std::function<void()> callback, uint64_t interval_ms = 0);

    // 取消定时任务
    bool cancel_timer(uint64_t timer_id);

    // 获取最近任务的超时时间（相对当前时间，单位毫秒）
    // 返回 -1 表示没有任务
    int64_t get_next_timeout_ms() const;

    // 处理所有到期任务
    void process_expired_timers();

    // 清理函数，避免堆积过多已取消节点
    void clean_canceled_timers();

private:
    std::priority_queue<TimerNode, std::vector<TimerNode>, TimerCompare> timers_;
    std::unordered_map<uint64_t, bool> canceled_map_; // 取消标记表，不立即删除，而是等到处理时跳过，即懒删除模式(Lazy Deletion)
    mutable std::mutex mutex_;
    uint64_t next_id_ = 1;

    // 清理已取消的节点
    void clean_canceled_timers();
};
