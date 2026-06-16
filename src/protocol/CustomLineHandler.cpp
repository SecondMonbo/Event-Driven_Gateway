#include "protocol/CustomLineHandler.hpp"
#include <ctime>
#include <sstream>

CustomLineHandler::CustomLineHandler(int conn_id, ClientConnection *conn) : conn_id_(conn_id), conn_(conn) {};

bool CustomLineHandler::process(std::string &read_buffer)
{
    std::string line;
    while (extract_line(read_buffer, line))
    {
        Message msg = parse(conn_id_, line);
        std::string resp = dispatch(msg) + "\n";
        conn_->send_data(resp);
    }
    return true;
}

void CustomLineHandler::reset()
{
    return;
}

std::string CustomLineHandler::dispatch(const Message &msg)
{
    if (msg.type() == "PING")
    {
        return "PONG|" + msg.payload();
    }
    else if (msg.type() == "CHAT")
    {
        return "CHAT_ACK|" + msg.payload();
    }
    else if (msg.type() == "TOOL")
    {
        if (msg.payload() == "time")
        {
            time_t now = time(nullptr);
            std::string tstr = ctime(&now);
            tstr.pop_back(); // remove newline
            return "TOOL_RES|" + tstr;
        }
        else
        {
            return "ERR|unknown_tool";
        }
    }
    else
    {
        return "ERR|unknown_type";
    }
}

bool CustomLineHandler::extract_line(std::string &buffer, std::string &line)
{
    size_t pos = buffer.find('\n');
    if (pos == std::string::npos)
        return false;
    line = buffer.substr(0, pos);
    // 去掉末尾的 '\r'（如果有）
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    buffer.erase(0, pos + 1);
    return true;
}

Message CustomLineHandler::parse(int conn_id, const std::string &line)
{
    size_t sep = line.find('|');
    if (sep == std::string::npos)
        return Message(conn_id, "ERR", "bad_format");
    std::string type = line.substr(0, sep);
    std::string payload = line.substr(sep + 1);
    return Message(conn_id, type, payload);
}