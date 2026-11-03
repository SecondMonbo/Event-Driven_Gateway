#include "llm/HttpClient.hpp"
#include <curl/curl.h>
#include <iostream>
#include <cstring>
#include <stdexcept>

bool HttpClient::post(
    const std::string &url,
    const std::string &body,
    std::function<void(const char *, size_t)> on_data,
    std::function<void(bool)> on_complete,
    long timeout_sec,
    const std::unordered_map<std::string, std::string> &extra_headers,
    const std::atomic<bool> *canceled)
{
    CURL *easy = curl_easy_init(); // 每次新建句柄
    CURLM *multi = curl_multi_init();

    if (!easy)
    {
        curl_multi_cleanup(multi);
        if (on_complete)
            on_complete(false);
        return false;
    }
    if (!multi)
    {
        curl_easy_cleanup(easy);
        if (on_complete)
            on_complete(false);
        return false;
    }

    // 重置句柄，清除之前请求的配置
    curl_easy_reset(easy);

    // 创建上下文（因为是同步阻塞，在请求期间有效）
    CallbackContext ctx;
    ctx.on_data = std::move(on_data);
    ctx.on_complete = std::move(on_complete);
    ctx.success = true;

    // 设置 URL
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());

    // 设置为 POST 请求
    curl_easy_setopt(easy, CURLOPT_POST, 1L);

    // 设置请求体
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));

    // 设置写回调函数（接收响应数据）
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);

    // 通过上下文指针传递用户数据
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &ctx);

    // 多线程环境下规避 SIGPIPE 信号
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);

    // 设置超时时间
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, timeout_sec * 1000L);

    // 设置请求头
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (headers)
    {
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);
    }

    // 添加额外头部行
    for (const auto &[key, value] : extra_headers)
    {
        std::string header = key + ": " + value;
        headers = curl_slist_append(headers, header.c_str());
    }
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headers);

    // 执行请求,手动
    CURLMcode add = curl_multi_add_handle(multi, easy);
    if (add != CURLM_OK)
    {
        if (headers)
            curl_slist_free_all(headers);

        curl_easy_cleanup(easy);
        curl_multi_cleanup(multi);
        if (on_complete)
            on_complete(false);
        return false;
    }

    int running = 0;
    CURLMcode mc = curl_multi_perform(multi, &running);
    bool aborted = false;

    while (mc == CURLM_OK && running > 0)
    {
        // 每轮检查 token
        if (canceled && canceled->load())
        {
            aborted = true;
            break;
        }

        int numfds = 0;
        mc = curl_multi_poll(multi, nullptr, 0, 100, &numfds);
        if (mc != CURLM_OK)
            break;

        mc = curl_multi_perform(multi, &running);
    }

    // 获取结果
    CURLcode result = CURLE_OK;
    if (!aborted)
    {
        int queued = 0;
        CURLMsg *msg = nullptr;

        while ((msg = curl_multi_info_read(multi, &queued)) != nullptr)
        {
            if (msg->msg == CURLMSG_DONE)
            {
                result = msg->data.result;
                break;
            }
        }
    }
    else
    {
        result = CURLE_ABORTED_BY_CALLBACK;
    }

    // 获取 HTTP 状态码
    long http_code = 0;
    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);

    curl_multi_remove_handle(multi, easy);
    curl_easy_cleanup(easy); // 用完销毁
    curl_multi_cleanup(multi);

    // 清理头部列表
    if (headers)
        curl_slist_free_all(headers);

    bool ok = !aborted && mc == CURLM_OK && result == CURLE_OK && http_code < 400;

    ctx.success = ok;
    if (ctx.on_complete)
        ctx.on_complete(ok);

    return ok;
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
