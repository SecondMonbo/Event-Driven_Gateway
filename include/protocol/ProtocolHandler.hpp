#pragma once

#include <string>
// 协议抽象基类：定义协议处理器统一接口
class ProtocolHandler
{
public:
    virtual ~ProtocolHandler() = default;

    // 处理读写缓冲区中的数据，返回true表示需要关闭
    virtual bool process(std::string &read_buffer) = 0;

    // 重置状态（用于Keep-Alive）
    virtual void reset() = 0;
};
