#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <cstring>
#include <string>

namespace fsx::transfer {

// CRC32 + SHA-256 for data integrity (Phase 4 + Phase 7)
// CRC32: per-chunk; SHA-256: whole-file final verification
class IntegrityService {
public:
  static constexpr size_t SHA256_SIZE = 32;

  // --- CRC32 (per-chunk) ---
  static uint32_t crc32(const void* data, size_t len);
  static uint32_t crc32(const std::vector<uint8_t>& data) {
    if (data.empty()) return 0;
    return crc32(data.data(), data.size());
  }
  static uint32_t crc32(const std::string& data) {
    if (data.empty()) return 0;
    return crc32(data.data(), data.size());
  }

  // --- SHA-256 (Phase 7: whole-file integrity) ---
  // Compute SHA-256 of in-memory data. Returns 32-byte digest or empty on error.
  static std::vector<uint8_t> sha256(const void* data, size_t len);
  static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data) {
    if (data.empty()) return sha256(nullptr, 0);
    return sha256(data.data(), data.size());
  }
  // Compute SHA-256 of file at path. Returns 32-byte digest or empty on error.
  static std::vector<uint8_t> sha256_file(const std::string& path);

  // Compare two 32-byte digests (e.g. computed vs client-provided)
  static bool sha256_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != SHA256_SIZE || b.size() != SHA256_SIZE) return false;
    return std::memcmp(a.data(), b.data(), SHA256_SIZE) == 0;
  }

private:
  static void init_crc_table();
  static bool crc_table_initialized_;
  static uint32_t crc_table_[256];
};

} // namespace fsx::transfer

