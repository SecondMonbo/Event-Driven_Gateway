#include "Dispatcher.hpp"
#include <ctime>
#include <sstream>

std::string Dispatcher::dispatch(const Message &msg)
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