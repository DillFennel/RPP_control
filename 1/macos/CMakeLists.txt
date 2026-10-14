cmake_minimum_required(VERSION 3.16)
project(SocketPracticeMac LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "This template targets macOS and uses SO_NOSIGPIPE.")
endif()

add_executable(server server/main.cpp)
add_executable(client client/main.cpp)

foreach(target server client)
    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
