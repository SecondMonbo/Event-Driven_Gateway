#include <gtest/gtest.h>

#include "tools/ToolExecutor.hpp"
#include "tools/ToolRegistry.hpp"
#include "tools/builtin/TimeTool.hpp"

#include <regex>
#include <stdexcept>

namespace
{
ToolCall make_call(const std::string &name)
{
    ToolCall call;
    call.id = "call_1";
    call.name = name;
    call.arguments = json::object();
    return call;
}
}

TEST(ToolsTest, MissingToolFails)
{
    ToolRegistry registry;
    ToolExecutor executor(registry);

    auto result = executor.execute_single(make_call("missing"));
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.result.find("not found"), std::string::npos);
}

TEST(ToolsTest, ToolExceptionFails)
{
    ToolRegistry registry;
    registry.registry_tool(
        "boom", "", json::object(),
        [](const json &) -> std::string
        {
            throw std::runtime_error("boom");
        });

    ToolExecutor executor(registry);
    auto result = executor.execute_single(make_call("boom"));
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.result.find("boom"), std::string::npos);
}

TEST(ToolsTest, SuccessfulToolReturnsSuccess)
{
    ToolRegistry registry;
    registry.registry_tool(
        "echo", "", json::object(),
        [](const json &) -> std::string
        {
            return "ok";
        });

    ToolExecutor executor(registry);
    auto result = executor.execute_single(make_call("echo"));
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.result, "ok");
}

TEST(ToolsTest, TimeToolProducesFormattedTime)
{
    ToolRegistry registry;
    register_time_tool(registry);
    ToolExecutor executor(registry);

    auto result = executor.execute_single(make_call("get_current_time"));
    EXPECT_TRUE(result.success);

    const std::regex pattern(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}$)");
    EXPECT_TRUE(std::regex_match(result.result, pattern)) << result.result;
}

TEST(ToolsTest, RegistryReportsExistingTool)
{
    ToolRegistry registry;
    EXPECT_FALSE(registry.exists("echo"));

    registry.registry_tool(
        "echo", "", json::object(),
        [](const json &) -> std::string
        {
            return "ok";
        });

    EXPECT_TRUE(registry.exists("echo"));
}
