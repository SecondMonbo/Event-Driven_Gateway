#include "core/EpollLoop.hpp"
#include "llm/HttpClient.hpp"
#include <iostream>

int main()
{
    HttpClient client;
    client.post(
        "http://localhost:8081/v1/chat/completions",
        R"({"model":"phi-3","messages":[{"role":"user","content":"你好"}],"stream":false})",
        [](const char *data, size_t len)
        {
            std::cout.write(data, len);
        },
        [](bool success)
        {
            std::cout << "\nRequest " << (success ? "succeeded" : "failed") << std::endl;
        });

    EpollLoop loop;
    if (!loop.init(12010))
    {
        std::cerr << "Failed to initialize gateway" << std::endl;
        return 1;
    }
    loop.run();
    return 0;
}