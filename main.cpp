#include <iostream>
#include <mqueue.h>
#include <unistd.h>
#include <fstream>
#include <errno.h>
#include <cstring>
#include <wait.h>
#include "toojpeg.h"
#include "logger.h"
#include "edf.h"
#include "attrs.h"

// Default params
int scenario_id = 0;
int width  = 32;
int height = 32;
int bytes_per_pixel = 3; // RGB

// Scheduling params
bool is_edf = false;
int sched_runtime = 1; // [ms]
int sched_period = 500; // [ms]
int sched_deadline = 4; // [ms]

// JPEG conversion params
const bool is_RGB = true; // true = RGB image, else false = grayscale
const auto quality = 90; // compression quality: 0 = worst, 100 = best, 80 to 90 are most often used
const bool downsample = false; // false = save as YCbCr444 JPEG (better quality), true = YCbCr420 (smaller file)
const char* comment = "example image"; // arbitrary JPEG comment

std::string prod_queue_name = "/prod_queue";
std::string log_queue_name = "/log_queue";

typedef struct Task {
    int id;
    long timestamp;
    int max_interval;
    unsigned char image[VECTOR_SIZE];
} Task;

// Output file
std::ofstream file;

// Write a single byte compressed by tooJpeg
void output(unsigned char byte) {
    file << byte;
}

void generateImage(unsigned char image[]) {
    // create a nice color transition
    for (auto y = 0; y < height; y++)
        for (auto x = 0; x < width; x++) {
            // memory location of current pixel
            auto offset = (y * width + x) * bytes_per_pixel;
            // red and green fade from 0 to 255, blue is always 127
            image[offset] = 255 * x / width;
            image[offset + 1] = 255 * y / height;
            image[offset + 2] = 127;
        }
}

void producer(struct mq_attr attr) {

    int local_task_id = getpid() + 1;
    int scheduling_result = -1;

    if (is_edf) {

        // Plan cyclic task
        struct EDF::sched_attr s_attr{};

        s_attr.size = sizeof(s_attr);
        s_attr.sched_flags = 0;
        s_attr.sched_nice = 0;
        s_attr.sched_priority = 0;

        /* creates a sched_runtime ms/sched_deadline ms reservation */
        s_attr.sched_policy = SCHED_DEADLINE;
        s_attr.sched_runtime = sched_runtime * 1000 * 1000;
        s_attr.sched_period = sched_period * 1000 * 1000;
        s_attr.sched_deadline = sched_deadline * 1000 * 1000;

        scheduling_result = EDF::sched_setattr(getpid(), &s_attr, 0);

    } else { // is FIFO
        struct sched_param sp{};
        sp.sched_priority = sched_get_priority_min(SCHED_RR);
        scheduling_result = sched_setscheduler(getpid(), SCHED_RR, &sp);
    }

    if (scheduling_result < 0) {
        std::string e(strerror(errno));
        Logger::logd(getpid(), Source::CLIENT, "Error scheduling task: " + e + ".");
        perror("sched_setattr");
        exit(-1);
    } else {

        // Open communication queue
        auto queue = mq_open(prod_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
        Logger::logd(getpid(), Source::PRODUCER,
                     "Opened queue. Id: " + std::to_string(queue) + ", errno: " + strerror(errno));

        for (int i = 0; i < 100; i++) {

            Logger::log(getpid(), local_task_id, Source::PRODUCER, "Started vector generation.");

            // New data generation
            Task task = {
                 local_task_id,
                 Logger::timestamp(),
                 sched_deadline,
                 NULL
            };

            generateImage(task.image);

            // Send generated data
            int result = mq_send(queue, (const char *) &task, MAX_MSG_SIZE, 2);
            Logger::log(getpid(), task.id, Source::PRODUCER,
                        "Sent vector. Length: " + std::to_string(sizeof(task)) +
                        ". Code result: " + std::to_string(result) + ", " + strerror(errno) + ".");

            /*Logger::Message message = {
                    getpid(),
                    task.id,
                    Source::PRODUCER,
                    "Sent msg. Length: " + std::to_string(sizeof(task)) +
                    ". Code result: " + std::to_string(result) + ", " + strerror(errno) + "."
            };
            Logger::qlog(message);*/

            // Cleanup state
            local_task_id++;
            sched_yield();
        }

        mq_close(queue);
    }
}

void* consumer(void* arg) {

    Task* task = (Task*) arg;

    // Prepare to output
    const auto file_name = "outputs/" + std::to_string(task->id) + ".jpeg";

    file.open(file_name, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);

    if(!file.is_open())
        Logger::log(task->id, task->id, Source::CLIENT,"Opening file  " + file_name + " failed");

    // Perform output action
    Logger::log(getpid(), task->id, Source::ENCODER, "Starting conversion to file: " + file_name + "...");
    auto ok = TooJpeg::writeJpeg(output, task->image, width, height, is_RGB, quality, downsample, comment);

    Logger::log(getpid(), task->id, Source::ARCHIVER,
                ok ? "Finished. Saved file as " + file_name : "Error saving file as " + file_name);
    file.close();

    return nullptr;
}

void client(struct mq_attr attr) {

    // Receive task from producer
    Task task;
    int ret = 1;

    // Open producer -> client queue
    auto prod_queue = mq_open(prod_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);

    do {

        Logger::logd(getpid(), Source::CLIENT,
                     "Opened queue. Id: " + std::to_string(prod_queue) + ", errno: " + strerror(errno));

        ret = mq_receive(prod_queue, (char *) &task, MAX_MSG_SIZE, NULL);
        Logger::logd(getpid(), Source::CLIENT,
                     "Received msg. Code result: " + std::to_string(ret) + ", errno: " + strerror(errno));

        pthread_t thread;
        pthread_create(&thread, NULL, consumer, &task);

    } while (ret > 0);

    mq_close(prod_queue);
}

int main(int argc, char * argv[]) {

    if (argc > 1) {
        for (int i = 0; i < argc; i++) {
            std::string s(argv[1]);
            if (s == "-d") {
                Logger::setDebug(true);
            } else if (s == "-e") {
                is_edf = true;
            }
        }
    }

    // Initialize queues params
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_maxmsg = MAX_MSGS;

    int pid = fork();
    if (pid) { //producer
        producer(attr);
        waitpid(pid, nullptr, 0);
        mq_unlink(prod_queue_name.c_str());
    } else { //child
        pid = fork();
        if (pid) { //consumer
            client(attr);
            waitpid(pid, nullptr, 0);
            mq_unlink(log_queue_name.c_str());
        } else { //logger
            Logger::logger(log_queue_name);
        }
    }

    Logger::logd(getpid(), Source::MAIN, "Exiting...");
    return 0;
}
