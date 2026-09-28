#include "core/ConnectionContext.hpp"
#include "core/ClientConnection.hpp"

ConnectionContext::ConnectionContext(std::weak_ptr<ClientConnection> c,
                                     ThreadPool &tp,
                                     TimerManager &tm, EpollLoop &l,
                                     LLMService &ls) : conn(c), thread_pool(tp), timer_manager(tm), loop(l), llm_service(&ls) {}

bool ConnectionContext::send_data(const std::string &data) const
{
    if (auto c = conn.lock())
    {
        c->send_data(data);
        return true;
    }
    else
    {
        return false;
    }
}