cmake_minimum_required(VERSION 3.16)
project(Gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
find_package(Threads REQUIRED)

# Core library: all production code except main.cpp.
# Excluding main lets unit tests link the real classes without duplicating sources.
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(gateway_core STATIC ${CORE_SOURCES})
target_include_directories(gateway_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CURL_INCLUDE_DIRS}
)
target_link_libraries(gateway_core PUBLIC ${CURL_LIBRARIES} Threads::Threads)

add_executable(gateway src/main.cpp)
target_link_libraries(gateway PRIVATE gateway_core)

enable_testing()
add_subdirectory(tests)
