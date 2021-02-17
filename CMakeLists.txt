cmake_minimum_required(VERSION 3.17)
project(coding_test C)

set(CMAKE_C_STANDARD 99)

find_package(PNG REQUIRED)
include_directories(${PNG_INCLUDE_DIR})

add_executable(coding_test png_transform.c)
target_link_libraries(coding_test ${PNG_LIBRARY})

