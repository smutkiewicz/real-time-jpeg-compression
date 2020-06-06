#include <iostream>
#include <mqueue.h>
#include <unistd.h>
#include <fstream>
#include <errno.h>
#include <cstring>
#include <wait.h>
#include "toojpeg.h"
#include "logger.h"

#define MAX_MSGS 10
#define MAX_MSG_SIZE 8192

// Default params
int p = 1;
int width  = 32;
int height = 32;
int bytes_per_pixel = 3; // RGB
int max_interval = 4; // 4x more than predicted speed

// JPEG conversion params
const bool isRGB = true; // true = RGB image, else false = grayscale
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
    int width;
    int height;
    int bytes_per_pixel;
    unsigned char image[3072];
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
    auto image = new unsigned char[3072];

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

    // Open communication queue
    auto queue = mq_open(prod_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
    Logger::logd(pid, source,
                "Opened queue. Id: " + std::to_string(queue) + ", errno: " + strerror(errno));

    // New data generation
    auto pixels = generateImage();
    Task task = {
            pid,
            Logger::timestamp(),
            max_interval,
            width,
            height,
            bytes_per_pixel,
            *pixels
    };

    // Send generated data
    int ret = mq_send(queue, (const char *) &task, sizeof(task), 2);
    Logger::log(pid, task.id, source,
            "Sent msg. Length: " + std::to_string(sizeof(task)) +
            "Code result: " + std::to_string(ret) + ", " + strerror(errno) + ".");
    mq_close(queue);

    delete[] pixels;
}

void consumer(const std::string& prod_queue_name, const std::string& log_queue_name, struct mq_attr attr)
{
    // Set global current source for logger
    source = Source::CLIENT;

    // Open producer -> consumer queue
    auto prod_queue = mq_open(prod_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);
    Logger::log(pid, Logger::DEBUG_TASK_ID, source,
                "Opened queue. Id: " + std::to_string(prod_queue) + ", errno: " + strerror(errno));

    // Receive task from producer
    Task task;
    int ret = mq_receive(prod_queue, (char *) &task, MAX_MSG_SIZE, NULL);
    Logger::logd(pid, source,
                "Received msg. Code result: " + std::to_string(ret) + ", errno: " + strerror(errno));

    // Prepare to output
    const auto file_name = "outputs/" + std::to_string(pid) + ".jpg";

    Logger::log(pid, task.id, source,"Opening file: " + file_name + "...");
    file.open(file_name, std::ios_base::out | std::ios_base::binary);

    // Perform output action
    Logger::log(pid, task.id, Source::ENCODER, "Starting conversion to file: " + file_name + "...");
    auto ok = TooJpeg::writeJpeg(output, task.image, width, height, isRGB, quality, downsample, comment);

    Logger::log(pid, task.id, Source::ARCHIVER,
            ok ? "Finished. Saved file as " + file_name : "Error saving file as " + file_name);
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
    // Adjust params
    width = width * p;
    height = height * p;
    max_interval = max_interval * p;

    // Initialize queues params
    std::string prod_queue_name = "/prod_queue";
    std::string log_queue_name = "/log_queue";
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
        if (pid) { //consumer
            consumer(prod_queue_name, log_queue_name, attr);
            waitpid(pid, nullptr, 0);
            mq_unlink(log_queue_name.c_str());
        } else {
            //logger
            //logger(log_queue_name, attr);
        }
    }

    Logger::logd(pid, source, "Exiting...");
    return 0;
}
