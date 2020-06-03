#include <string>

#ifndef SCZR00_LOGGER_H
#define SCZR00_LOGGER_H

namespace Source
{
    auto const PRODUCER = "Producer";
    auto const ENCODER = "Encoder";
    auto const CLIENT = "Client";
    auto const ARCHIVER = "Archiver";
}

namespace Logger
{
    void log(int mid, const std::string& source, const std::string& message);
    long timestamp();
}

#endif //SCZR00_LOGGER_H