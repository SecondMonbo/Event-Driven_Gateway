#ifndef DISPATCHER_HPP
#define DISPATCHER_HPP

#include <string>
#include "Message.hpp"

class Dispatcher
{
public:
    static std::string dispatch(const Message &msg);
};

#endif