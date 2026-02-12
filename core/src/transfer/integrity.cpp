#include "fsx/transfer/integrity.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <openssl/evp.h>

namespace fsx::transfer {

// CRC32 polynomial (reversed): 0xEDB88320
static constexpr uint32_t CRC32_POLY = 0xEDB88320UL;

bool IntegrityService::crc_table_initialized_ = false;
uint32_t IntegrityService::crc_table_[256];

void IntegrityService::init_crc_table() {
  if (crc_table_initialized_) return;
  
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t crc = i;
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ CRC32_POLY;
      } else {
        crc >>= 1;
      }
    }
    crc_table_[i] = crc;
  }
  
  crc_table_initialized_ = true;
}

uint32_t IntegrityService::crc32(const void* data, size_t len) {
  if (!data || len == 0) return 0;
  
  init_crc_table();
  
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
  uint32_t crc = 0xFFFFFFFFUL;
  
  for (size_t i = 0; i < len; i++) {
    uint8_t idx = (crc ^ bytes[i]) & 0xFF;
    crc = (crc >> 8) ^ crc_table_[idx];
  }
  
  return crc ^ 0xFFFFFFFFUL;
}

// --- SHA-256 (Phase 7) ---
std::vector<uint8_t> IntegrityService::sha256(const void* data, size_t len) {
  std::vector<uint8_t> digest(SHA256_SIZE, 0);
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return {};
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
      EVP_DigestUpdate(ctx, data, len) != 1 ||
      EVP_DigestFinal_ex(ctx, digest.data(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return {};
  }
  EVP_MD_CTX_free(ctx);
  return digest;
}

std::vector<uint8_t> IntegrityService::sha256_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return {};
  if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return {};
  }
  char buf[8192];
  while (f.read(buf, sizeof(buf)) || f.gcount() > 0) {
    if (EVP_DigestUpdate(ctx, buf, static_cast<size_t>(f.gcount())) != 1) {
      EVP_MD_CTX_free(ctx);
      return {};
    }
    if (!f) break;
  }
  std::vector<uint8_t> digest(SHA256_SIZE, 0);
  if (EVP_DigestFinal_ex(ctx, digest.data(), nullptr) != 1) {
    EVP_MD_CTX_free(ctx);
    return {};
  }
  EVP_MD_CTX_free(ctx);
  return digest;
}

} // namespace fsx::transfer

