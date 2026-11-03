#include <gtest/gtest.h>

#include "llm/SessionManager.hpp"

#include <chrono>
#include <thread>

namespace
{
ChatMessage make_user(const std::string &text)
{
    return ChatMessage("user", text);
}

ChatMessage make_assistant_tool_call()
{
    ChatMessage msg("assistant", "");
    msg.tool_calls.push_back({"call_1", "get_current_time", "{}"});
    return msg;
}
}

TEST(SessionManagerTest, TrimsAtUserBoundaryWithoutSystem)
{
    SessionManager sm(4, 3600);
    sm.append_message("s", make_user("q1"));
    sm.append_message("s", ChatMessage("assistant", "a1"));
    sm.append_message("s", make_user("q2"));
    sm.append_message("s", ChatMessage("assistant", "a2"));
    sm.append_message("s", make_user("q3"));
    sm.append_message("s", ChatMessage("assistant", "a3"));

    auto history = sm.get_history("s");
    ASSERT_EQ(history.size(), 4u);
    EXPECT_EQ(history[0].role, "user");
    EXPECT_EQ(history[0].content, "q2");
    EXPECT_EQ(history[2].role, "user");
    EXPECT_EQ(history[2].content, "q3");
}

TEST(SessionManagerTest, PreservesLeadingSystemMessages)
{
    SessionManager sm(4, 3600);
    sm.append_message("s", ChatMessage("system", "sys1"));
    sm.append_message("s", ChatMessage("system", "sys2"));
    sm.append_message("s", make_user("q1"));
    sm.append_message("s", ChatMessage("assistant", "a1"));
    sm.append_message("s", make_user("q2"));
    sm.append_message("s", ChatMessage("assistant", "a2"));
    sm.append_message("s", make_user("q3"));

    auto history = sm.get_history("s");
    ASSERT_GE(history.size(), 2u);
    EXPECT_EQ(history[0].role, "system");
    EXPECT_EQ(history[0].content, "sys1");
    EXPECT_EQ(history[1].role, "system");
    EXPECT_EQ(history[1].content, "sys2");

    // 不允许以孤立 tool 开头
    EXPECT_NE(history.front().role, "tool");
}

TEST(SessionManagerTest, KeepsToolCallGroupTogether)
{
    SessionManager sm(4, 3600);
    sm.append_message("s", make_user("q1"));
    sm.append_message("s", make_assistant_tool_call());
    sm.append_message("s", ChatMessage("tool", "tool-result"));
    sm.append_message("s", ChatMessage("assistant", "a1"));
    sm.append_message("s", make_user("q2"));

    auto history = sm.get_history("s");
    ASSERT_FALSE(history.empty());
    EXPECT_NE(history.front().role, "tool");

    // 如果保留 assistant(tool_calls)，后面必须有 tool 消息
    for (size_t i = 0; i < history.size(); ++i)
    {
        if (history[i].role == "assistant" && !history[i].tool_calls.empty())
        {
            ASSERT_LT(i + 1, history.size());
            EXPECT_EQ(history[i + 1].role, "tool");
        }
    }
}

TEST(SessionManagerTest, CleanupExpiredSessions)
{
    SessionManager sm(100, 0);
    sm.append_message("s", make_user("hi"));
    ASSERT_EQ(sm.get_history("s").size(), 1u);

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    sm.cleanup_expired_sessions();
    EXPECT_TRUE(sm.get_history("s").empty());
}

TEST(SessionManagerTest, GetRecentMessages)
{
    SessionManager sm(100, 3600);
    sm.append_message("s", make_user("q1"));
    sm.append_message("s", ChatMessage("assistant", "a1"));
    sm.append_message("s", make_user("q2"));

    auto recent = sm.get_recent_messages("s", 2);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0].content, "a1");
    EXPECT_EQ(recent[1].content, "q2");

    EXPECT_TRUE(sm.get_recent_messages("s", -1).empty());
}
