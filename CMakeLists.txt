cmake_minimum_required(VERSION 3.16)
project(sczr00)

set(CMAKE_CXX_STANDARD 14)

add_executable(sczr00 main.cpp toojpeg.cpp toojpeg.h)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(sczr00 rt Threads::Threads)