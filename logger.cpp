#include <chrono>
#include "logger.h"

namespace Logger
{
    bool debug = false;

    long timestamp() {
        // Unix timestamp
        const auto p1 = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::seconds>(p1.time_since_epoch()).count();
    }

    // timestamp/process_id/task_id/source:.....message.....
    void log(int pid, int task_id, const std::string &source, const std::string &message) {
        printf("%ld/%05d/%05d/%-8s: %s\n", timestamp(), pid, task_id, source.c_str(), message.c_str());
    }

    // NOTE: task_id=-1 is reserved for debug messages
    void logd(int pid, const std::string &source, const std::string &message) {
        if (debug)
            printf("%ld/%05d/%05d/%-8s: %s\n", timestamp(), pid, DEBUG_TASK_ID, source.c_str(), message.c_str());
    }

    void setDebug(bool val) {
        debug = val;
    }
}
