cmake_minimum_required(VERSION 3.20)
project(fsx_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fsx_core
  src/main.cpp
  src/net/tcp_server.cpp
  src/net/tcp_session.cpp
)

target_include_directories(fsx_core PRIVATE include)
target_link_libraries(fsx_core PRIVATE pthread)
