cmake_minimum_required(VERSION 3.20)
project(cpp_TaskScheduler)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create executable
add_executable(cpp_TaskScheduler
    src/main.cpp
    src/core/ThreadPool.cpp
)

# Include root src directory
target_include_directories(cpp_TaskScheduler PRIVATE src)