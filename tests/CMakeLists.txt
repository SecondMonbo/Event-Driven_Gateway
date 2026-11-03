include(FetchContent)

# CMake 3.24+ warns about downloaded archive timestamps unless CMP0135 is set.
# The policy guard keeps the test build compatible with older CMake versions.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
FetchContent_MakeAvailable(googletest)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(add_gateway_unit_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE gateway_core GTest::gtest_main)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endfunction()

add_gateway_unit_test(test_session_manager unit/test_session_manager.cpp)
add_gateway_unit_test(test_timer_manager unit/test_timer_manager.cpp)
add_gateway_unit_test(test_sse_format unit/test_sse_format.cpp)
add_gateway_unit_test(test_tools unit/test_tools.cpp)
add_gateway_unit_test(test_thread_pool unit/test_thread_pool.cpp)

add_test(
    NAME gateway_integration
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/test_gateway.py
            --gateway $<TARGET_FILE:gateway>
            --www ${CMAKE_SOURCE_DIR}/www
)
set_tests_properties(gateway_integration PROPERTIES
    TIMEOUT 90
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
