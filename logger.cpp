#include <chrono>
#include <utility>
#include <mqueue.h>
#include <iostream>
#include "logger.h"
#include "attrs.h"

namespace Logger {

    bool debug = false;
    std::string logger_queue_name = "/log_queue";

    void logger(std::string name) {

        logger_queue_name  = std::move(name);
        struct mq_attr attr{};
        attr.mq_flags = 0;
        attr.mq_msgsize = MAX_MSG_SIZE;
        attr.mq_maxmsg = MAX_MSGS;

        auto log_queue = mq_open(logger_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);
        int ret;

        do {

            Logger::Message message;
            ret = mq_receive(log_queue, (char *) &message, MAX_MSG_SIZE, NULL);
            Logger::log(message);

        } while (ret > 0);

        mq_close(log_queue);
    }

    long timestamp() {
        // Unix timestamp
        const auto p1 = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(p1.time_since_epoch()).count();
    }

    // send via communication queue
    void qlog(Logger::Message message) {
        struct mq_attr attr{};
        attr.mq_flags = 0;
        attr.mq_msgsize = MAX_MSG_SIZE;
        attr.mq_maxmsg = MAX_MSGS;

        auto queue = mq_open(logger_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
        mq_send(queue, (const char *) &message, MAX_MSG_SIZE, 2);
        mq_close(queue);
    }

    // timestamp/process_id/task_id/source:.....message.....
    void log(int pid, int task_id, const std::string &source, const std::string &message) {
        printf("%ld/%05d/%05d/%-8s: %s\n", timestamp(), pid, task_id, source.c_str(), message.c_str());
        fflush(stdout);
    }

    void log(const Logger::Message& log) {
        printf("%ld/%05d/%05d/%-8s: %s\n", timestamp(), log.pid, log.task_id, log.source.c_str(), log.message.c_str());
        fflush(stdout);
    }

    // NOTE: task_id=-1 is reserved for debug messages
    void logd(int pid, const std::string &source, const std::string &message) {
        if (debug) {
            printf("%ld/%05d/%05d/%-8s: %s\n", timestamp(), pid, DEBUG_TASK_ID, source.c_str(), message.c_str());
            fflush(stdout);
        }
    }

    void setDebug(bool val) {
        debug = val;
    }
}
