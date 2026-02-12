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

# --- zlib (Phase 6: compression) ---
find_package(ZLIB REQUIRED)

# --- libopus (Phase 10: voice) ---
pkg_check_modules(OPUS REQUIRED opus)

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
  # Phase 3: File transfer
  src/transfer/transfer_manager.cpp
  src/storage/file_store.cpp
  # Phase 4: Integrity (CRC32)
  src/transfer/integrity.cpp
  # Phase 5: Resume functionality
  src/storage/resume_store.cpp
  # Phase 6: Compression
  src/transfer/zlib_codec.cpp
  # Phase 8: Crypto (AES-GCM + RSA key exchange)
  src/crypto/aes_gcm.cpp
  src/crypto/rsa.cpp
  # Phase 9: Throttling
  src/transfer/throttler.cpp
  # Phase 10: Voice chat
  src/voice/opus_codec.cpp
  src/voice/jitter_buffer.cpp
  src/voice/voice_manager.cpp
  src/net/udp_voice_server.cpp
)

target_include_directories(fsx_core PRIVATE
  ${Boost_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
  ${LIBPQ_INCLUDE_DIRS}
  ${OPUS_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(fsx_core PRIVATE
  Boost::system
  Boost::thread
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
  ${LIBPQ_LIBRARIES}
  ZLIB::ZLIB
  ${OPUS_LIBRARIES}
)

target_compile_options(fsx_core PRIVATE ${LIBPQ_CFLAGS_OTHER})

# Test executable for IntegrityService (CRC32 + SHA-256 Phase 7)
add_executable(test_integrity
  tests/test_integrity.cpp
  src/transfer/integrity.cpp
)

target_include_directories(test_integrity PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(test_integrity PRIVATE OpenSSL::Crypto)

# Test executable for ZlibCodec (Phase 6)
add_executable(test_zlib_codec
  tests/test_zlib_codec.cpp
  src/transfer/zlib_codec.cpp
)
target_include_directories(test_zlib_codec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_zlib_codec PRIVATE ZLIB::ZLIB)
add_test(NAME ZlibCodecTest COMMAND test_zlib_codec)

# Test executable for AesGcm (Phase 8)
add_executable(test_aes_gcm
  tests/test_aes_gcm.cpp
  src/crypto/aes_gcm.cpp
)
target_include_directories(test_aes_gcm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${OPENSSL_INCLUDE_DIR})
target_link_libraries(test_aes_gcm PRIVATE OpenSSL::Crypto)
add_test(NAME AesGcmTest COMMAND test_aes_gcm)

# Test executable for RSA key exchange (Phase 8)
add_executable(test_rsa
  tests/test_rsa.cpp
  src/crypto/rsa.cpp
  src/crypto/aes_gcm.cpp
)
target_include_directories(test_rsa PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${OPENSSL_INCLUDE_DIR})
target_link_libraries(test_rsa PRIVATE OpenSSL::Crypto)
add_test(NAME RsaKeyExchangeTest COMMAND test_rsa)

# Enable testing
enable_testing()
add_test(NAME IntegrityServiceTest COMMAND test_integrity)
