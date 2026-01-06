cmake_minimum_required(VERSION 3.20)
project(fsx_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(fsx_client src/main.cpp)
target_link_libraries(fsx_client PRIVATE pthread)

# Test executables
find_package(Boost REQUIRED COMPONENTS system)

add_executable(test_auth src/test_auth.cpp)
target_link_libraries(test_auth PRIVATE Boost::system pthread)

add_executable(test_persistent src/test_persistent.cpp)
target_link_libraries(test_persistent PRIVATE Boost::system pthread)

add_executable(test_file_transfer src/test_file_transfer.cpp)
target_link_libraries(test_file_transfer PRIVATE Boost::system pthread)