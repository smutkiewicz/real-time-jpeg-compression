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
void output(unsigned char byte)
{
    file << byte;
}

unsigned char* generateImage()
{
    auto image = new unsigned char[VECTOR_SIZE];

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

    return image;
}

void producer(const std::string& prod_queue_name, struct mq_attr attr)
{
    // Global current source for logger
    source = Source::PRODUCER;
    int local_task_id = pid + 1;

    // Open communication queue
    auto queue = mq_open(prod_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
    Logger::logd(pid, source,
                "Opened queue. Id: " + std::to_string(queue) + ", errno: " + strerror(errno));

    // Plan cyclic task
    struct EDF::sched_attr s_attr{};
    int ret;

    s_attr.size = sizeof(attr);
    s_attr.sched_flags = 0;
    s_attr.sched_nice = 0;
    s_attr.sched_priority = 0;

    /* creates a 10ms/30ms reservation */
    s_attr.sched_policy = SCHED_DEADLINE;
    s_attr.sched_runtime = 1 * 1000 * 1000;
    s_attr.sched_period = 1000 * 1000 * 1000;
    s_attr.sched_deadline = 5 * 1000 * 1000;

    ret = EDF::sched_setattr(getpid(), &s_attr, 0);
    if (ret < 0)
    {
        Logger::logd(pid, Source::CLIENT, "Error scheduling EDF task...");
        perror("sched_setattr");
        exit(-1);
    }
    else
    {
        for (int i = 0; i < 100; i++) {

            // New data generation
            auto pixels = generateImage();
            Task task = {
                 local_task_id,
                 Logger::timestamp(),
                 max_interval,
                 *pixels
            };

            // Send generated data (identified by local_task_id)
            int result = mq_send(queue, (const char *) &task, sizeof(task), 2);
            Logger::log(pid, task.id, source,
                        "Sent msg. Length: " + std::to_string(sizeof(task)) +
                        ". Code result: " + std::to_string(result) + ", " + strerror(errno) + ".");

            // Cleanup state
            delete[] pixels;
            local_task_id++;

            sched_yield();
        }
    }

    mq_close(queue);
}

void* consumer(void* arg)
{
    Task* task = (Task*) arg;

    // Prepare to output
    const auto file_name = "outputs/" + std::to_string(pid) + ".jpg";

    Logger::log(pid, task->id, Source::CLIENT,"Opening file: " + file_name + "...");
    file.open(file_name, std::ios_base::out | std::ios_base::binary);

    // Perform output action
    Logger::log(pid, task->id, Source::ENCODER, "Starting conversion to file: " + file_name + "...");
    auto ok = TooJpeg::writeJpeg(output, task->image, width, height, is_RGB, quality, downsample, comment);

    Logger::log(pid, task->id, Source::ARCHIVER,
                ok ? "Finished. Saved file as " + file_name : "Error saving file as " + file_name);

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

        task.target_pid = fork(); // child's pid
        if (task.target_pid) {
            // parent
        } else { //child
            task.target_pid = getpid();
            pthread_t thread;
            pthread_create(&thread, NULL, consumer, &task);
        }

    } while (ret > 0);

    mq_close(prod_queue);
}

void logger(const std::string& log_queue_name, struct mq_attr attr)
{
    char rec[MAX_MSG_SIZE];
    auto log_queue = mq_open(log_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);
    std::cout<<"log open: "<<log_queue<<" "<<strerror(errno)<<std::endl;
    int ret = mq_receive(log_queue, rec, MAX_MSG_SIZE, NULL);
    std::cout<<"log rec: "<<ret<<" "<<strerror(errno)<<std::endl;
    std::cout<<"log: "<<rec<<std::endl;
    mq_close(log_queue);
}

int main(int argc, char * argv[])
{
    if (argc > 1) scenario_id = atoi(argv[1]);

    std::string prod_queue_name = "/prod_queue";
    std::string log_queue_name = "/log_queue";
    
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
        pid = fork();
        if (pid) { //client
            client(prod_queue_name, attr);
            waitpid(pid, nullptr, 0);
            mq_unlink(log_queue_name.c_str());
        }
    }

    Logger::logd(pid, source, "Exiting...");
    return 0;
}
