#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <utility>
#include <type_traits>
#include <stdexcept>

class ThreadPool
{
public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency()); // explicit 禁止隐式传参
    ~ThreadPool();
    // 禁止拷贝和赋值
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // 提交异步任务
    template <typename Task, typename Callback>
    void enqueue_with_callback(Task &&task, Callback &&callback);

    // 主线程调用：处理所有已完成任务的回调（非阻塞）
    void process_completions();

    void stop(); // 停止线程池，等待所有任务完成

    size_t worker_count() const { return workers_.size(); };

private:
    void worker_loop();
    void post_completion(std::function<void()> callback);

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_;
    std::mutex tasks_mutex_;
    std::condition_variable tasks_cv_;

    std::queue<std::function<void()>> completions_;
    std::mutex completions_mutex_;

    std::atomic<bool> stop_;
};

// 模板实现
template <typename Task, typename Callback>
void ThreadPool::enqueue_with_callback(Task &&task, Callback &&callback)
{
    auto wrapper = [this, task = std::forward<Task>(task), callback = std::forward<Callback>(callback)]() mutable
    {
        using ResultType = decltype(task());

        // 这是 C++17 引入的编译时条件语句。它的条件必须是一个编译期常量表达式（例如 true/false，或 std::is_void_v<...> 这种编译时求值的值）。
        // 如果条件为 true，编译器会编译并保留对应的分支（{ task(); ... }）。
        // 如果条件为 false，编译器会完全丢弃该分支（甚至不检查其语法正确性，但要求整体语法上合法，比如大括号内的语句不依赖不成立的模板参数）。
        if constexpr (std::is_void_v<ResultType>)
        {
            task();
            post_completion([callback = std::move(callback)]()
                            { callback(); });
        }
        else
        {
            auto result = task();
            post_completion([callback = std::move(callback), result = std::move(result)]() mutable
                            { callback(std::move(result)); });
        }
    };
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (stop_)
        {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        task_.emplace(std::move(wrapper));
    }
    tasks_cv_.notify_one();
}