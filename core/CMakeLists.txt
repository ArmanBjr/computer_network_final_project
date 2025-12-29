cmake_minimum_required(VERSION 3.20)
project(fsx_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

# --- Boost ---
find_package(Boost REQUIRED COMPONENTS system thread)

# --- OpenSSL ---
find_package(OpenSSL REQUIRED)

# --- libpq (PostgreSQL C client) ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED libpq)

add_executable(fsx_core
  src/main.cpp
  src/net/tcp_server.cpp
  src/net/tcp_session.cpp
  src/net/auth_handler.cpp
  src/net/session_manager.cpp
  src/storage/db_client.cpp
  src/storage/db_config.cpp
  # New DB layer files
  src/db/db.cpp
  src/db/user_repository.cpp
  src/db/session_repository.cpp
  src/auth/password_hash.cpp
)

target_include_directories(fsx_core PRIVATE
  ${Boost_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
  ${LIBPQ_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(fsx_core PRIVATE
  Boost::system
  Boost::thread
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
  ${LIBPQ_LIBRARIES}
)

target_compile_options(fsx_core PRIVATE ${LIBPQ_CFLAGS_OTHER})
