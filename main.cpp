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

#define MAX_MSGS 10
#define VECTOR_SIZE 3072
#define MAX_MSG_SIZE 8192

// Default params
int scenario_id = 0;
int p = 1;
int width  = 32;
int height = 32;
int bytes_per_pixel = 3; // RGB
int max_interval = 4; // 4x more than predicted speed

// JPEG conversion params
const bool is_RGB = true; // true = RGB image, else false = grayscale
const auto quality = 90; // compression quality: 0 = worst, 100 = best, 80 to 90 are most often used
const bool downsample = false; // false = save as YCbCr444 JPEG (better quality), true = YCbCr420 (smaller file)
const char* comment = "example image"; // arbitrary JPEG comment

// current process data
int pid = 0;
std::string source = Source::MAIN;

typedef struct Task {
    int id;
    long timestamp;
    int max_interval;
    unsigned char image[VECTOR_SIZE];
} Task;

// Output file
std::ofstream file;

// Write a single byte compressed by tooJpeg
void output(unsigned char byte){
    file << byte;
}

void generateImage(unsigned char image[] ){

    // create a nice color transition (replace with your code)
    for (auto y = 0; y < height; y++)
        for (auto x = 0; x < width; x++)
        {
            // memory location of current pixel
            auto offset = (y * width + x) * bytes_per_pixel;
            // red and green fade from 0 to 255, blue is always 127
            image[offset] = 255 * x / width;
            image[offset + 1] = 255 * y / height;
            image[offset + 2] = 127;
        }

    Logger::logd(pid, Source::PRODUCER, "New vector generated.");
}

void producer(const std::string& prod_queue_name, struct mq_attr attr){
    // Global current source for logger
    source = Source::PRODUCER;

    // Open communication queue
    auto queue = mq_open(prod_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
    Logger::logd(pid, source,
                "Opened queue. Id: " + std::to_string(queue) + ", errno: " + strerror(errno));

    // New data generation
    Task task = {
            pid,
            Logger::timestamp(),
            max_interval,
            NULL
    };
    generateImage(task.image);
    // Send generated data
    int ret = mq_send(queue, (const char *) &task, MAX_MSG_SIZE, 2);
    Logger::log(pid, task.id, source,
            "Sent msg. Length: " + std::to_string(sizeof(task)) +
            ". Code result: " + std::to_string(ret) + ", " + strerror(errno) + ".");
    mq_close(queue);

}

void* consumer(void* arg){
    Task* task = (Task*) arg;

    if (scenario_id == 2)
    {
        struct EDF::sched_attr attr{};
        int x = 0, ret;
        unsigned int flags = 0;

        printf("deadline thread start %ld\n", gettid());

        attr.size = sizeof(attr);
        attr.sched_flags = 0;
        attr.sched_nice = 0;
        attr.sched_priority = 0;

        /* creates a 10ms/30ms reservation */
        attr.sched_policy = SCHED_DEADLINE;
        attr.sched_runtime = 10 * 1000 * 1000;
        attr.sched_period = 30 * 1000 * 1000;
        attr.sched_deadline = 30 * 1000 * 1000;

        ret = EDF::sched_setattr(0, &attr, flags);
        if (ret < 0)
        {
            Logger::log(pid, task->id, Source::CLIENT, "Error scheduling EDF task...");
            exit(-1);
        }
    }

    // Prepare to output
    const auto file_name = "outputs/" + std::to_string(pid) + ".jpeg";

    Logger::log(pid, task->id, Source::CLIENT,"Opening file: " + file_name + "...");
    file.open(file_name, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
    if(!file.is_open()) Logger::log(pid, task->id, Source::CLIENT,"Opening file  " + file_name + " failed");

    // Perform output action
    Logger::log(pid, task->id, Source::ENCODER, "Starting conversion to file: " + file_name + "...");
    auto ok = TooJpeg::writeJpeg(output, task->image, width, height, is_RGB, quality, downsample, comment);

    Logger::log(pid, task->id, Source::ARCHIVER,
                ok ? "Finished. Saved file as " + file_name : "Error saving file as " + file_name);
    file.close();
    return nullptr;
}

void client(const std::string& prod_queue_name, struct mq_attr attr)
{
    // Set global current source for logger
    source = Source::CLIENT;

    // Open producer -> client queue
    auto prod_queue = mq_open(prod_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);
    Logger::log(pid, Logger::DEBUG_TASK_ID, source,
                "Opened queue. Id: " + std::to_string(prod_queue) + ", errno: " + strerror(errno));

    // Receive task from producer
    Task task;
    int ret = 1;

    do
    {
        ret = mq_receive(prod_queue, (char *) &task, MAX_MSG_SIZE, NULL);
        Logger::logd(pid, source,
                     "Received msg. Code result: " + std::to_string(ret) + ", errno: " + strerror(errno));

        pthread_t thread;
        pthread_create(&thread, NULL, consumer, &task);

    } while (ret > 0);

    mq_close(prod_queue);
}

int main(int argc, char * argv[])
{
    if (argc > 1) {
        scenario_id = atoi(argv[1]);
    }

    std::string prod_queue_name = "/prod_queue";
    
    // Adjust params
    width = width * p;
    height = height * p;
    max_interval = max_interval * p;

    // Initialize queues params
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_maxmsg = MAX_MSGS;

    pid = fork();
    if (pid) { //producer
        producer(prod_queue_name, attr);
        waitpid(pid, nullptr, 0);
        mq_unlink(prod_queue_name.c_str());
    } else { //child
        client(prod_queue_name, attr);
    }

    Logger::logd(pid, source, "Exiting...");
    return 0;
}
