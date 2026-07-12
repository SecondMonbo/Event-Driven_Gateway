#pragma once

#include <curl/curl.h>
#include <string>
#include <functional>
#include <memory>

/**
 * @brief 轻量级 HTTP 客户端封装 (基于libcurl)
 *
 * 职责：
 *   - 发起 HTTP POST 请求 （支持 JSON 请求体）
 *   - 支持流式响应回调 （逐块接受数据）
 *   - 自动管理 libcurl 资源 （RAII）
 *
 * 使用示例：
 *   HttpClient client;
 *   client.post(
 *       "http://localhost:8081/v1/chat/completions",
 *       R"({"model":"phi-3","messages":[{"role":"user","content":"你好"}],"stream":true})",
 *       [](const char* data, size_t len) {
 *           // 处理每个数据块（如解析 SSE 格式）
 *           std::cout << "chunk: " << std::string(data, len) << std::endl;
 *       },
 *       [](bool success) {
 *           std::cout << "Request " << (success ? "succeeded" : "failed") << std::endl;
 *       }
 *   );
 */

class HttpClient
{
public:
    HttpClient();
    ~HttpClient();

    // 禁止拷贝
    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    /**
     * @brief 发起 POST 请求 （支持流式响应）
     *
     * @param url      请求地址 （如 http://localhost:8081/v1/chat/completions）
     * @param body     JSON 格式请求体
     * @param on_data  数据块回调: 每收到一块数据时调用（data指针，数据长度）
     * @param on_complete 请求完成回调：success 为 true 表示成功，false 表示失败
     * @param timeout_sec 超时时间 （秒，默认60）
     *
     * @return true 请求已成功发起（不代表请求成功，成功与否由 on_complete 指示）
     * @return false 请求发起失败 （如 URL 无效、libcurl初始化失败等)
     */

    bool post(
        const std::string &url,
        const std::string &body,
        std::function<void(const char *, size_t)> on_data,
        std::function<void(bool)> on_complete,
        long timeout_sec = 60);

private:
    // libcurl句柄
    CURL *curl_;

    // 内部上下文，用于在静态回调中传递用户数据
    struct CallbackContext
    {
        std::function<void(const char *, size_t)> on_data;
        std::function<void(bool)> on_complete;
        bool success = true;
        std::string error_msg;
    };

    // 静态回调函数（由libcurl调用）
    static size_t write_callback(char *ptr, size_t size, size_t nmemb, void *userdata);

    // 辅助：设置通用选项
    void set_commom_options(const std::string &url);
};