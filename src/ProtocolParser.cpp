#include "ProtocolParser.hpp"
#include <string>
#include <cctype>

bool ProtocolParser::extract_line(std::string &buffer, std::string &line)
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

Message ProtocolParser::parse(int conn_id, const std::string &line)
{
    size_t sep = line.find('|');
    if (sep == std::string::npos)
        return Message(conn_id, "ERR", "bad_format");
    std::string type = line.substr(0, sep);
    std::string payload = line.substr(sep + 1);
    return Message(conn_id, type, payload);
}