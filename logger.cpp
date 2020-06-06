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

    // timestamp/process_id/task_id/source:.....message.....
    void log(int pid, int task_id, const std::string &source, const std::string &message)
    {
        std::cout << timestamp() << "/" << pid << "/" << task_id << "/" << source << ": " << message << std::endl;
    }

    // NOTE: task_id=-1 is reserved for debug messages
    void logd(int pid, const std::string &source, const std::string &message)
    {
        std::cout << timestamp() << "/" << pid << "/" << DEBUG_TASK_ID << "/" << source << ": " << message << std::endl;
    }
}
