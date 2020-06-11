#include <iostream>
#include <mqueue.h>
#include <unistd.h>
#include <fstream>
#include <errno.h>
#include <cstring>
#include <wait.h>
#include <unistd.h>
#include "toojpeg.h"
#include "logger.h"
#include "edf.h"

#define MAX_MSGS 10
#define VECTOR_SIZE 3072
#define MAX_MSG_SIZE 8192

// Default params
int scenario_id = 0;
int width  = 32;
int height = 32;
int bytes_per_pixel = 3; // RGB

int sched_runtime = 1; // [ms]
int sched_period = 2000; // [ms]
int sched_deadline = 4; // [ms] 4x more than predicted speed

// JPEG conversion params
const bool is_RGB = true; // true = RGB image, else false = grayscale
const auto quality = 90; // compression quality: 0 = worst, 100 = best, 80 to 90 are most often used
const bool downsample = false; // false = save as YCbCr444 JPEG (better quality), true = YCbCr420 (smaller file)
const char* comment = "example image"; // arbitrary JPEG comment

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

    Logger::logd(getpid(), Source::PRODUCER, "New vector generated.");
}

void producer(const std::string& prod_queue_name, struct mq_attr attr) {

    int local_task_id = getpid() + 1;

    // Plan cyclic task
    struct EDF::sched_attr s_attr{};
    int ret;

    s_attr.size = sizeof(s_attr);
    s_attr.sched_flags = 0;
    s_attr.sched_nice = 0;
    s_attr.sched_priority = 0;

    /* creates a 10ms/30ms reservation */
    s_attr.sched_policy = SCHED_DEADLINE;
    s_attr.sched_runtime = sched_runtime * 1000 * 1000;
    s_attr.sched_period = sched_period * 1000 * 1000;
    s_attr.sched_deadline =  sched_runtime * 1000 * 1000;

    ret = EDF::sched_setattr(getpid(), &s_attr, 0);
    if (ret < 0) {
        std::string e(strerror(errno));
        Logger::logd(getpid(), Source::CLIENT, "Error scheduling EDF task: " + e + ".");
        perror("sched_setattr");
        exit(-1);
    } else {
        for (int i = 0; i < 10; i++) {

            // Open communication queue
            auto queue = mq_open(prod_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
            Logger::logd(getpid(), Source::PRODUCER,
                         "Opened queue. Id: " + std::to_string(queue) + ", errno: " + strerror(errno));

            // New data generation
            Task task = {
                 local_task_id,
                 Logger::timestamp(),
                 max_deadline,
                 NULL
            };

            generateImage(task.image);

            // Send generated data
            int result = mq_send(queue, (const char *) &task, MAX_MSG_SIZE, 2);
            Logger::log(getpid(), task.id, Source::PRODUCER,
                        "Sent msg. Length: " + std::to_string(sizeof(task)) +
                        ". Code result: " + std::to_string(result) + ", " + strerror(errno) + ".");

            // Cleanup state
            mq_close(queue);
            local_task_id++;
            sched_yield();
        }
    }
}

void* consumer(void* arg) {

    Task* task = (Task*) arg;

    // Prepare to output
    const auto file_name = "outputs/" + std::to_string(task->id) + ".jpeg";

    Logger::log(getpid(), task->id, Source::CLIENT,"Opening file: " + file_name + "...");
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

void client(const std::string& prod_queue_name, struct mq_attr attr) {

    // Receive task from producer
    Task task;
    int ret = 1;

    do {

        // Open producer -> client queue
        auto prod_queue = mq_open(prod_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);
        Logger::logd(getpid(), Source::CLIENT,
                     "Opened queue. Id: " + std::to_string(prod_queue) + ", errno: " + strerror(errno));

        ret = mq_receive(prod_queue, (char *) &task, MAX_MSG_SIZE, NULL);
        Logger::logd(getpid(), Source::CLIENT,
                     "Received msg. Code result: " + std::to_string(ret) + ", errno: " + strerror(errno));

        pthread_t thread;
        pthread_create(&thread, NULL, consumer, &task);

        mq_close(prod_queue);

    } while (ret > 0);
}

int main(int argc, char * argv[]) {

    if (argc > 1) {
        for (int i = 0; i < argc; i++) {
            if (argv[i] == "-d") {
                Logger::setDebug(true);
            }
        }
    }

    if (argc > 2)
        scenario_id = atoi(argv[2]);

    std::string prod_queue_name = "/prod_queue";

    // Initialize queues params
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_maxmsg = MAX_MSGS;

    int pid = fork();
    if (pid) { //producer
        producer(prod_queue_name, attr);
        waitpid(pid, nullptr, 0);
        mq_unlink(prod_queue_name.c_str());
    } else { //child
        client(prod_queue_name, attr);
    }

    Logger::logd(getpid(), Source::MAIN, "Exiting...");
    return 0;
}
