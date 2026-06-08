#ifndef MESSAGE_HPP
#define MESSAGE_HPP

#include <string>

class Message
{
public:
    Message(int conn_id, const std::string &type, const std::string &payload)
        : conn_id_(conn_id), type_(type), payload_(payload) {}

    int conn_id() const { return conn_id_; }
    const std::string &type() const { return type_; }
    const std::string &payload() const { return payload_; }

private:
    int conn_id_;
    std::string type_;
    std::string payload_;
};

#endif