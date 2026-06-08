#ifndef PROTOCOL_PARSER_HPP
#define PROTOCOL_PARSER_HPP

#include <string>
#include "Message.hpp"

class ProtocolParser
{
public:
    static bool extract_line(std::string &buffer, std::string &line);
    static Message parse(int conn_id, const std::string &line);
};

#endif