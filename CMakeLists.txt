cmake_minimum_required(VERSION 3.10)
project(Gateway)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O0 -g")

include_directories(include)

file(GLOB_RECURSE SOURCES "src/*.cpp")

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

include_directories(${CURL_INCLUDE_DIRS})
link_directories(${CURL_LIBRARY_DIRS})

add_executable(gateway ${SOURCES})
target_link_libraries(gateway ${CURL_LIBRARIES} pthread)



#add_executable(test_threadpool test/test_threadpool.cpp src/ThreadPool.cpp)
#target_link_libraries(test_threadpool pthread)
#target_include_directories(test_threadpool PRIVATE include)