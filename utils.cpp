//
// Created by ubuntu on 6/7/20.
//

#include "utils.h"

namespace Utils
{
    std::string prod_queue_name(int pid)
    {
        return prod_queue_prefix + std::to_string(pid);
    }
}