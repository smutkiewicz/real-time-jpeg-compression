#include <iostream>
#include <mqueue.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <wait.h>
#include "toojpeg.h"

#define MAX_MSGS 10
#define MAX_MSG_SIZE 8192

void myOutput(unsigned char Byte){}

void producer(std::string prod_queue_name, struct mq_attr attr){
    std::string msg = "kupa";
    auto queue = mq_open(prod_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
    std::cout<<"p open: "<<queue<<" "<<strerror(errno)<<std::endl;
    auto pixels = new unsigned char[5*5*3];
    TooJpeg::writeJpeg(myOutput,pixels,5,5);
    int ret = mq_send(queue,msg.c_str(),msg.length()*sizeof(char),2);
    std::cout<<"p send: "<<ret<<" "<<strerror(errno)<<std::endl;
    mq_close(queue);
}

void consumer(std::string prod_queue_name, std::string log_queue_name, struct mq_attr attr){
    char rec[MAX_MSG_SIZE];
    auto prod_queue = mq_open(prod_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);
    std::cout<<"con open prod: "<<prod_queue<<" "<<strerror(errno)<<std::endl;
    auto log_queue = mq_open(log_queue_name.c_str(), O_WRONLY | O_CREAT , 0777, &attr);
    std::cout<<"con open log: "<<log_queue<<" "<<strerror(errno)<<std::endl;
    int ret = mq_receive(prod_queue, rec, MAX_MSG_SIZE, NULL);
    std::cout<<"con rec: "<<ret<<" "<<strerror(errno)<<std::endl;
    std::cout<<"con: "<<rec<<std::endl;
    ret = mq_send(log_queue, rec,strlen(rec),2);
    std::cout<<"con send: "<<ret<<" "<<strerror(errno)<<std::endl;
    mq_close(prod_queue);
    mq_close(log_queue);
}

void logger(std::string log_queue_name, struct mq_attr attr){
    char rec[MAX_MSG_SIZE];
    auto log_queue = mq_open(log_queue_name.c_str(), O_RDONLY | O_CREAT , 0777, &attr);
    std::cout<<"log open: "<<log_queue<<" "<<strerror(errno)<<std::endl;
    int ret = mq_receive(log_queue, rec, MAX_MSG_SIZE, NULL);
    std::cout<<"log rec: "<<ret<<" "<<strerror(errno)<<std::endl;
    std::cout<<"log: "<<rec<<std::endl;
    mq_close(log_queue);
}

int main() {

    std::string prod_queue_name = "/prod_queue";
    std::string log_queue_name = "/log_queue";
    struct mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_maxmsg = MAX_MSGS;

    auto pid = fork();
    if (pid){ //producer
        producer(prod_queue_name, attr);
        waitpid(pid, nullptr, 0);
        mq_unlink(prod_queue_name.c_str());
    }
    else { //child
        pid = fork();
        if (pid){//consumer
            consumer(prod_queue_name, log_queue_name, attr);
            waitpid(pid, nullptr, 0);
            mq_unlink(log_queue_name.c_str());
        }
        else{//logger
            logger(log_queue_name, attr);
        }
    }
    std::cout<<"Koniec"<<std::endl;
    return 0;
}
