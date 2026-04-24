cmake_minimum_required(VERSION 3.10)
project(problem_068_tlsf CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(code
    src/main.cpp
    src/allocator.cpp
)

target_include_directories(code PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Optimize for speed; disable exceptions not required
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

