#include <string>

#ifndef SCZR00_LOGGER_H
#define SCZR00_LOGGER_H

namespace Source
{
    auto const MAIN = "Main";
    auto const PRODUCER = "Producer";
    auto const ENCODER = "Encoder";
    auto const CLIENT = "Client";
    auto const ARCHIVER = "Archiver";
}

namespace Logger
{
    auto const DEBUG_TASK_ID = -1;

    void log(int pid, int task_id, const std::string &source, const std::string &message);
    void logd(int pid, const std::string &source, const std::string &message);
    long timestamp();
}

#endif //SCZR00_LOGGER_H