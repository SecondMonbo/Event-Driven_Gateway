#include <gtest/gtest.h>

#include "protocol/SseHandler.hpp"

TEST(SseFormatTest, FormatsSingleLineContent)
{
    EXPECT_EQ(
        SseHandler::format_sse_message("hello", "content"),
        "event: content\ndata: hello\n\n");
}

TEST(SseFormatTest, SplitsMultilineData)
{
    EXPECT_EQ(
        SseHandler::format_sse_message("a\nb", "content"),
        "event: content\ndata: a\ndata: b\n\n");
}

TEST(SseFormatTest, SupportsEmptyData)
{
    EXPECT_EQ(
        SseHandler::format_sse_message("", "heartbeat"),
        "event: heartbeat\ndata: \n\n");
}

TEST(SseFormatTest, SupportsId)
{
    EXPECT_EQ(
        SseHandler::format_sse_message("x", "heartbeat", "42"),
        "event: heartbeat\nid: 42\ndata: x\n\n");
}

TEST(SseFormatTest, ChunkFrame)
{
    EXPECT_EQ(SseHandler::chunk("abc"), "3\r\nabc\r\n");
    EXPECT_EQ(SseHandler::chunk(""), "");
    EXPECT_EQ(SseHandler::chunk_end(), "0\r\n\r\n");
}
