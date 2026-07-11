#pragma once

#include "ProtocolHandler.hpp"
#include "core/ClientConnection.hpp"
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

class CustomLineHandler : public ProtocolHandler
{
public:
    CustomLineHandler(int conn_id, ClientConnection *conn);
    ProcessResult process(std::string &read_buffer) override;
    void reset() override;

    static bool extract_line(std::string &buffer, std::string &line);
    static Message parse(int conn_id, const std::string &line);

    static std::string dispatch(const Message &msg);

private:
    int conn_id_;
    ClientConnection *const conn_;
};