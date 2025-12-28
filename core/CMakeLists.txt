cmake_minimum_required(VERSION 3.20)
project(fsx_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fsx_core
  src/main.cpp
  src/net/tcp_server.cpp
  src/net/tcp_session.cpp
  core/src/storage/db_client.cpp
  core/src/storage/db_config.cpp
)

find_package(OpenSSL REQUIRED)
find_library(PQ_LIB pq REQUIRED)

target_include_directories(fsx_core PRIVATE /usr/include/postgresql)

target_link_libraries(fsx_core
  PRIVATE
    ${PQ_LIB}
    OpenSSL::Crypto
)

target_include_directories(fsx_core PRIVATE include)
target_link_libraries(fsx_core PRIVATE pthread)

find_package(Boost REQUIRED COMPONENTS system thread)

target_link_libraries(fsx_core PRIVATE Boost::system Boost::thread pq ssl crypto)

find_package(PostgreSQL REQUIRED)
target_include_directories(fsx_core PRIVATE ${PostgreSQL_INCLUDE_DIRS})
target_link_libraries(fsx_core PRIVATE ${PostgreSQL_LIBRARIES})
