#include "llm/SessionManager.hpp"

SessionManager::SessionManager(int max_history, int max_idle_seconds) : max_history_(max_history), max_idle_seconds_(max_idle_seconds) {};

std::vector<ChatMessage> SessionManager::get_history(const std::string &session_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end())
    {
        return it->second;
    }
    return {};
};

void SessionManager::append_message(const std::string &session_id, const ChatMessage &msg)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto &vec = sessions_[session_id];
    vec.push_back(msg);

    // 裁剪避免超限
    if (vec.size() > static_cast<size_t>(max_history_))
    {
        vec.erase(vec.begin(), vec.begin() + (vec.size() - max_history_));
    }
    last_activity_[session_id] = std::chrono::steady_clock::now();
};

std::vector<ChatMessage> SessionManager::get_recent_messages(const std::string &session_id, int n)
{
    if (n < 0)
        return {};
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end())
        return {};

    const auto &vec = it->second;
    int count = std::min(n, static_cast<int>(vec.size()));
    return std::vector<ChatMessage>(vec.end() - count, vec.end());
};

void SessionManager::cleanup_expired_sessions()
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();
    std::vector<std::string> to_remove;

    // 两次遍历删除，避免在循环体中使迭代器失效
    for (const auto &[id, lastime] : last_activity_)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastime).count();
        if (elapsed > max_idle_seconds_)
        {
            to_remove.push_back(id);
        }
    }

    for (const auto &id : to_remove)
    {
        sessions_.erase(id);
        last_activity_.erase(id);
    }
}

void SessionManager::set_max_history(int max_messages)
{
    max_history_ = max_messages;
}