#include "EpollLoop.hpp"
#include <iostream>

int main()
{
    EpollLoop loop;
    if (!loop.init(12010))
    {
        std::cerr << "Failed to initialize gateway" << std::endl;
        return 1;
    }
    loop.run();
    return 0;
}