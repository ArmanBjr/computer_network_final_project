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

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
add_executable(test_file_transfer
  src/test_file_transfer.cpp
  ../core/src/transfer/integrity.cpp
  ../core/src/transfer/zlib_codec.cpp
  ../core/src/crypto/rsa.cpp
  ../core/src/crypto/aes_gcm.cpp
)
target_include_directories(test_file_transfer PRIVATE ../core/include)
target_link_libraries(test_file_transfer PRIVATE Boost::system pthread ZLIB::ZLIB OpenSSL::Crypto)

# Phase 10: Voice test client
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)
add_executable(test_voice src/test_voice.cpp)
target_include_directories(test_voice PRIVATE ../core/include ${OPUS_INCLUDE_DIRS})
target_link_libraries(test_voice PRIVATE Boost::system pthread ${OPUS_LIBRARIES})