cmake_minimum_required(VERSION 3.20)
project(fsx_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fsx_client src/main.cpp)
target_link_libraries(fsx_client PRIVATE pthread)
