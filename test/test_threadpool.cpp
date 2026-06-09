#include "ThreadPool.hpp"
#include <iostream>
#include <chrono>

int main()
{
    ThreadPool tp(2);
    tp.enqueue_with_callback(
        []() -> std::string
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return "hello";
        },
        [](std::string result)
        {
            std::cout << result << std::endl;
        });
    // 等待足够长时间，并不断处理完成队列
    for (int i = 0; i < 20; ++i)
    {
        tp.process_completions();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // 或者简单等待 200ms 后再处理一次
    // std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // tp.process_completions();
    return 0;
}