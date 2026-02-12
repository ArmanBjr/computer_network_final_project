#include "fsx/transfer/zlib_codec.h"
#include <zlib.h>
#include <stdexcept>
#include <cstring>

namespace fsx::transfer {

std::vector<uint8_t> ZlibCodec::compress(const void* data, size_t len) {
  if (len == 0) return {};
  uLongf dest_len = compressBound(static_cast<uLong>(len));
  std::vector<uint8_t> out(dest_len);
  int ret = compress2(
    out.data(),
    &dest_len,
    static_cast<const Bytef*>(data),
    static_cast<uLong>(len),
    Z_DEFAULT_COMPRESSION
  );
  if (ret != Z_OK) {
    return {};
  }
  out.resize(dest_len);
  return out;
}

std::vector<uint8_t> ZlibCodec::decompress(const void* data, size_t len, size_t uncompressed_size) {
  if (len == 0 || uncompressed_size == 0) return {};
  std::vector<uint8_t> out(uncompressed_size);
  uLongf dest_len = static_cast<uLongf>(uncompressed_size);
  int ret = uncompress(
    out.data(),
    &dest_len,
    static_cast<const Bytef*>(data),
    static_cast<uLong>(len)
  );
  if (ret != Z_OK) {
    return {};
  }
  out.resize(dest_len);
  return out;
}

} // namespace fsx::transfer
