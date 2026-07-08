#pragma once

#include <string>
#include <iostream>

// 处理结果枚举类，定义程序返回值，用以HTTP协议升级SSE协议和WebSocket协议
enum class ProcessResult
{
    CONTINUE,         // 正常处理完成，需要保持连接
    CLOSE,            // 需要关闭连接
    UPGRADE_SSE,      // 切换到 SSE 协议
    UPGRADE_WEBSOCKET // 切换到 WebSocket 协议
};

// 协议抽象基类：定义协议处理器统一接口
class ProtocolHandler
{
public:
    virtual ~ProtocolHandler() = default;

    // 处理读写缓冲区中的数据，返回true表示需要关闭
    virtual ProcessResult process(std::string &read_buffer) = 0;

    // 重置状态（用于Keep-Alive）
    virtual void reset() = 0;
};
