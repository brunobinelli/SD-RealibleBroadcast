cmake_minimum_required(VERSION 3.15)
project(DifusaoConfiavel LANGUAGES CXX)

# ── Language standard ────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Default build type ───────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Debug | Release)" FORCE)
endif()

# ── Executable ───────────────────────────────────────────────
add_executable(servidor
    src/main.cpp
    src/ReliableBroadcast.cpp
    src/HttpServer.cpp
)

# Headers live in the same directory as CMakeLists.txt
target_include_directories(servidor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ── Compiler warnings ────────────────────────────────────────
target_compile_options(servidor PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
        -Wall -Wextra -Wpedantic -Wno-unused-parameter>
)

# ── Threads (pthreads on Linux/macOS) ────────────────────────
find_package(Threads REQUIRED)
target_link_libraries(servidor PRIVATE Threads::Threads)

# ── Convenience message ──────────────────────────────────────
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Output     : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")