//
// Created by ubuntu on 6/7/20.
//

#ifndef SCZR00_UTILS_H
#define SCZR00_UTILS_H

#include <string>

namespace Utils
{
    // queues names, target names are constructed from taskid/producerprocessid
    std::string prod_queue_prefix = "/prod_queue/";
    std::string log_queue_prefix = "/log_queue/";

    std::string prod_queue_name(int pid);
}

#endif //SCZR00_UTILS_H
