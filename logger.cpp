#include <chrono>
#include <iostream>
#include "logger.h"

namespace Logger
{
    long timestamp()
    {
        // Unix timestamp
        const auto p1 = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count();
    }

    void log(int id, const std::string &source, const std::string &message)
    {
        std::cout << timestamp() << "/" << id << "/" << source << ": " << message << std::endl;
    }
}
