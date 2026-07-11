#include "ThreadPool.hpp"
#include <iostream>

ThreadPool::ThreadPool(size_t threads) : stop_(false)
{
    for (size_t i = 0; i < threads; ++i)
    {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool()
{
    stop();
}

void ThreadPool::stop()
{
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (stop_)
            return;
        stop_ = true;
    }
    tasks_cv_.notify_all();
    for (auto &worker : workers_)
    {
        if (worker.joinable())
            worker.join();
    }
}

void ThreadPool::worker_loop()
{
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait(lock, [this]
                           { return stop_ || !task_.empty(); });
            if (stop_ && task_.empty())
                return;
            task = std::move(task_.front());
            task_.pop();
        }
        task();
    }
}

void ThreadPool::post_completion(std::function<void()> callback)
{
    if (!callback)
    {
        std::cerr << "Warning: trying to post empty completion callback" << std::endl;
        return;
    }
    std::lock_guard<std::mutex> lock(completions_mutex_);
    completions_.push(std::move(callback));
}

void ThreadPool::process_completions()
{
    std::function<void()> callback;
    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(completions_mutex_);
            if (completions_.empty())
                break;
            callback = std::move(completions_.front());
            completions_.pop();
        }
        if (!callback)
        {
            std::cerr << "Warning: empty completion callback popped" << std::endl;
            continue;
        }
        callback();
    }
}