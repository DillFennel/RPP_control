cmake_minimum_required(VERSION 3.16)
project(SocketPracticeMac LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "Build this macOS example on macOS.")
endif()

add_executable(server server/server.cpp)
add_executable(client client/client.cpp)

foreach(target server client)
    target_compile_features(${target} PRIVATE cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()
