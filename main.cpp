#include <iostream>
#include <mqueue.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <wait.h>
#include "toojpeg.h"
#include "logger.h"

#define MAX_MSGS 10
#define MAX_MSG_SIZE 8192

// Default params
int p = 1;
int width  = 254;
int height = 254;
int bytes_per_pixel = 3;
int max_interval = 4; // 4x more than predicted speed

// Task ids for logger
int producer_task_id = 1;
int consumer_task_id = 1;

typedef struct Task {
    int msg_id;
    long timestamp;
    int max_interval;
    int width;
    int height;
    int bytes_per_pixel;
    unsigned char* image;
} Task;

unsigned char* generateImage(int task_id)
{
    auto image = new unsigned char[width * height * bytes_per_pixel];

    // create a nice color transition (replace with your code)
    for (auto y = 0; y < height; y++)
    {
        for (auto x = 0; x < width; x++)
        {
            // memory location of current pixel
            auto offset = (y * width + x) * bytes_per_pixel;
            // red and green fade from 0 to 255, blue is always 127
            image[offset] = 255 * x / width;
            image[offset + 1] = 255 * y / height;
            image[offset + 2] = 127;
        }
    }

    Logger::log(task_id, Source::PRODUCER, "New vector generated.");

    return image;
}

void producer(std::string prod_queue_name, struct mq_attr attr)
{
    auto queue = mq_open(prod_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
    Logger::log(producer_task_id,Source::PRODUCER,
                "Opened queue. " + std::to_string(queue) + ", errno: " + strerror(errno));

    auto pixels = generateImage(producer_task_id);
    Task task = {
            producer_task_id,
            Logger::timestamp(),
            max_interval,
            width,
            height,
            bytes_per_pixel,
            pixels
    };

    int ret = mq_send(queue, (const char *) &task, sizeof(task), 2);

    Logger::log(producer_task_id,Source::PRODUCER,
            "Sent msg. Code result: " + std::to_string(ret) + ", " + strerror(errno) + ".");
    mq_close(queue);

    // increment task id for logs
    producer_task_id++;
}

void consumer(std::string prod_queue_name, std::string log_queue_name, struct mq_attr attr)
{
    auto prod_queue = mq_open(prod_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);
    Logger::log(consumer_task_id,Source::CLIENT,
                "Opened queue. " + std::to_string(prod_queue) + ", errno: " + strerror(errno));

    //auto log_queue = mq_open(log_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
    //std::cout<<"con open log: "<<log_queue<<" "<<strerror(errno)<<std::endl;

    Task* task;
    int ret = mq_receive(prod_queue, (char *) &task, sizeof(task), NULL);
    Logger::log(consumer_task_id,Source::CLIENT,
                "Received msg. Code result: " + std::to_string(ret) + ", errno: " + strerror(errno));

    //std::cout<<"con: " << task<<std::endl;
    //ret = mq_send(log_queue, rec,strlen(rec),2); // why this?
    //std::cout<<"con send: "<<ret<<" "<<strerror(errno)<<std::endl;
    mq_close(prod_queue);
    //mq_close(log_queue);

    // increment task id for logs
    consumer_task_id++;
}

void logger(std::string log_queue_name, struct mq_attr attr)
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
    // Parse arguments if they are passed
    if (argc > 1) p = atoi(argv[1]); // param for scaling data size

    // Adjust params
    width = width * p;
    height = height * p;
    max_interval = max_interval * p;

    // testing msgsize
    Task sample_task = {
            0,
            Logger::timestamp(),
            max_interval,
            width,
            height,
            bytes_per_pixel,
            new unsigned char[width * height * bytes_per_pixel]
    };

    // initialize queues params
    std::string prod_queue_name = "/prod_queue";
    std::string log_queue_name = "/log_queue";
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_msgsize = sizeof(sample_task) + 1024;
    attr.mq_maxmsg = MAX_MSGS;

    auto pid = fork();
    if (pid) {
        //producer
        producer(prod_queue_name, attr);
        waitpid(pid, nullptr, 0);
        mq_unlink(prod_queue_name.c_str());
    } else {
        //child
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

    std::cout << "Exiting" << std::endl;
    return 0;
}
