#pragma once

#include <cstddef>
#include <vector>
#include <cstdint>

namespace fsx::transfer {

// Phase 6: zlib compression/decompression for chunk data.
// Uses zlib format (compress2 / uncompress) for compatibility.
class ZlibCodec {
public:
  // Compress raw data. Returns compressed bytes (empty on error).
  static std::vector<uint8_t> compress(const void* data, size_t len);
  static std::vector<uint8_t> compress(const std::vector<uint8_t>& data) {
    if (data.empty()) return {};
    return compress(data.data(), data.size());
  }

  // Decompress compressed data. |uncompressed_size| is the expected uncompressed size
  // (from protocol original_size). Returns uncompressed bytes, or empty on error.
  static std::vector<uint8_t> decompress(const void* data, size_t len, size_t uncompressed_size);
  static std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, size_t uncompressed_size) {
    if (data.empty()) return {};
    return decompress(data.data(), data.size(), uncompressed_size);
  }
};

} // namespace fsx::transfer
