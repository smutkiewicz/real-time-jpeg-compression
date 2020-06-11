#include <iostream>
#include <mqueue.h>
#include <unistd.h>
#include <fstream>
#include <errno.h>
#include <cstring>
#include <wait.h>
#include <chrono>
#include <thread>
#include "toojpeg.h"
#include "logger.h"
#include "edf.h"

#define MAX_MSGS 10
#define VECTOR_SIZE 3072
#define MAX_MSG_SIZE 8192
#define SAMPLES 10000

// Default params
int scenario_id = 3;
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
    int process_id;
    int img_id;
    std::chrono::time_point<std::chrono::system_clock> send;
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
            0,
            std::chrono::system_clock::now(),
            max_interval,
            NULL
    };
    int ret = 0;
    int i = 0;
    for (;i<SAMPLES && ret==0;++i) {
        generateImage(task.image);
        task.img_id=i;
        task.send=std::chrono::system_clock::now();
        // Send generated data
        ret = mq_send(queue, (const char *) &task, MAX_MSG_SIZE, 2);
        Logger::log(pid, task.img_id, source,
                    "Sent msg. Length: " + std::to_string(sizeof(task)) +
                    ". Code result: " + std::to_string(ret) + ", " + strerror(errno) + ".");
    }
    Logger::logd(pid, source, "Send "+std::to_string(i));
    mq_close(queue);

}

void* consumer(void* arg){
    Task* task = (Task*) arg;

    // Prepare to output
    const auto file_name = "outputs/" + std::to_string(pid) + ".jpeg";

    Logger::log(pid, task->img_id, Source::CLIENT, "Opening file: " + file_name + "...");
    file.open(file_name, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
    if(!file.is_open()) Logger::log(pid, task->img_id, Source::CLIENT, "Opening file  " + file_name + " failed");

    // Perform output action
    Logger::log(pid, task->img_id, Source::ENCODER, "Starting conversion to file: " + file_name + "...");
    auto ok = TooJpeg::writeJpeg(output, task->image, width, height, is_RGB, quality, downsample, comment);

    Logger::log(pid, task->img_id, Source::ARCHIVER,
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
    std::chrono::time_point<std::chrono::system_clock> diff;
    std::ofstream log;
    std::string log_file_name = "outputs/log_sc";
    log_file_name+=std::to_string(scenario_id);
    log_file_name+=".txt";
    log.open(log_file_name);
    int i=0;
    for(; i<SAMPLES && ret>0; ++i){
        ret = mq_receive(prod_queue, (char *) &task, MAX_MSG_SIZE, NULL);
        std::chrono::duration<double> diff = std::chrono::system_clock::now()-task.send;
        log<<task.img_id<<" "<<diff.count()<<std::endl;
        Logger::logd(pid, source,
                     "Received msg. Code result: " + std::to_string(ret) + ", errno: " + strerror(errno));
//        pthread_t thread;
//        pthread_create(&thread, nullptr, consumer, &task);
        consumer((void*)&task);
    }
    log.close();
    Logger::logd(pid, source, "Received "+std::to_string(i));
    mq_close(prod_queue);
}

int main(int argc, char * argv[])
{
    if (argc > 1) {
        scenario_id = atoi(argv[1]);
    }
    auto cpus_no=std::thread::hardware_concurrency();
    std::cout<<"Dostępne są "<<cpus_no<<" procesory"<<std::endl;

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

    // Initialize affinity structure
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    pid = fork();
    if (pid) { //producer
        if (scenario_id) {
            switch(scenario_id){
                case 1: CPU_SET(cpus_no-1,&cpu_set);
                case 2: CPU_SET(cpus_no-2,&cpu_set);
                case 3: CPU_SET(cpus_no-2,&cpu_set);
            }
            sched_setaffinity(0,sizeof(cpu_set_t), &cpu_set);
        }
        producer(prod_queue_name, attr);
        waitpid(pid, nullptr, 0);
        mq_unlink(prod_queue_name.c_str());
    }
    else { //child
        if(scenario_id) {
            switch(scenario_id){
                case 1: CPU_SET(cpus_no-1,&cpu_set);
                case 2: CPU_SET(cpus_no-1,&cpu_set);
                case 3: CPU_SET(cpus_no-1,&cpu_set);
            }
            sched_setaffinity(0,sizeof(cpu_set_t), &cpu_set);
        }
        client(prod_queue_name, attr);
    }

    Logger::logd(pid, source, "Exiting...");
    return 0;
}
