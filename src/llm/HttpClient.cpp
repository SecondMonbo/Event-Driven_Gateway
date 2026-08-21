#include "llm/HttpClient.hpp"
#include <curl/curl.h>
#include <iostream>
#include <cstring>
#include <stdexcept>

// 初始化和析构 libcurl 句柄
HttpClient::HttpClient()
{
    curl_ = curl_easy_init();
    if (!curl_)
        throw std::runtime_error("Failed to initialize libcurl");
}

HttpClient::~HttpClient()
{
    if (curl_)
    {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

bool HttpClient::post(
    const std::string &url,
    const std::string &body,
    std::function<void(const char *, size_t)> on_data,
    std::function<void(bool)> on_complete,
    long timeout_sec,
    const std::unordered_map<std::string, std::string> &extra_headers)
{
    if (!curl_)
    {
        if (on_complete)
            on_complete(false);
        return false;
    }

    // 重置句柄，清楚之前请求的配置
    curl_easy_reset(curl_);

    // 创建上下文（因为是同步阻塞，在请求期间有效）
    CallbackContext ctx;
    ctx.on_data = std::move(on_data);
    ctx.on_complete = std::move(on_complete);
    ctx.success = true;

    // 设置 URL
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());

    // 设置为 POST 请求
    curl_easy_setopt(curl_, CURLOPT_POST, 1L);

    // 设置请求体
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));

    // 设置超时（秒）
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, timeout_sec);

    // 设置写回调函数（接收响应数据）
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, write_callback);

    // 通过上下文指针传递用户数据
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &ctx);

    // 设置请求头
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (headers)
    {
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
    }

    // 添加额外头部行
    for (const auto &[key, value] : extra_headers)
    {
        std::string header = key + ": " + value;
        headers = curl_slist_append(headers, header.c_str());
    }
    curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

    std::cout << "=== HttpClient::post ===" << std::endl;
    std::cout << "URL: " << url << std::endl;
    for (const auto &[key, value] : extra_headers)
    {
        std::cout << "Header: " << key << ": " << value << std::endl;
    }

    // 执行请求（阻塞直到完成）
    CURLcode res = curl_easy_perform(curl_);

    // 获取 HTTP 状态码
    long http_code = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

    // 检测错误, 分开判断，将来调试有需要方便记录错误信息
    if (res != CURLE_OK)
    {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
        ctx.success = false;
    }
    if (http_code >= 400)
    {
        std::cerr << "HTTP error: " << http_code << std::endl;
        ctx.success = false;
    }

    // 清理头部列表
    if (headers)
        curl_slist_free_all(headers);

    // 调用完成回调（如果存在）
    if (ctx.on_complete)
    {
        ctx.on_complete(ctx.success);
    }

    return (res == CURLE_OK && http_code < 400);
}

size_t HttpClient::write_callback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    size_t total_size = size * nmemb;
    auto *ctx = static_cast<CallbackContext *>(userdata);
    if (ctx && ctx->on_data)
    {
        ctx->on_data(ptr, total_size);
    }
    return total_size;
}