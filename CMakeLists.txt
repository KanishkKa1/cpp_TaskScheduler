cmake_minimum_required(VERSION 3.20)

project(cpp_TaskScheduler LANGUAGES CXX)

# ========================
# C++ Standard
# ========================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ========================
# Find Threads
# ========================
find_package(Threads REQUIRED)

# ========================
# Core Library
# ========================
add_library(threadpool_core
    src/core/ThreadPool.cpp
    src/core/Worker.cpp
)

target_include_directories(threadpool_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(threadpool_core
    PUBLIC
        Threads::Threads
)

# ========================
# Executable
# ========================
add_executable(cpp_TaskScheduler
    src/main.cpp
)

target_link_libraries(cpp_TaskScheduler
    PRIVATE
        threadpool_core
)

# ========================
# Compiler Warnings (Production hygiene)
# ========================
if (MSVC)
    target_compile_options(threadpool_core PRIVATE /W4)
    target_compile_options(cpp_TaskScheduler PRIVATE /W4)
else()
    target_compile_options(threadpool_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(cpp_TaskScheduler PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ========================
# Build Type Defaults
# ========================
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ========================
# Optional: Debug flags
# ========================
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")