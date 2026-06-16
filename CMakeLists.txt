cmake_minimum_required(VERSION 3.10)
project(Gateway)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O0 -g")

include_directories(include)

file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(gateway ${SOURCES})

add_executable(test_threadpool test/test_threadpool.cpp src/ThreadPool.cpp)
target_link_libraries(test_threadpool pthread)
target_include_directories(test_threadpool PRIVATE include)