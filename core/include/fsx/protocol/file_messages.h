#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <arpa/inet.h>

// Portable 64-bit endian conversion helpers
namespace {
  inline uint64_t be64toh_portable(uint64_t x) {
    #if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      return __builtin_bswap64(x);
    #elif defined(_WIN32) || defined(_WIN64)
      return _byteswap_uint64(x);
    #else
      // Fallback: manual byte swap
      return ((x & 0xFF00000000000000ULL) >> 56) |
             ((x & 0x00FF000000000000ULL) >> 40) |
             ((x & 0x0000FF0000000000ULL) >> 24) |
             ((x & 0x000000FF00000000ULL) >> 8)  |
             ((x & 0x00000000FF000000ULL) << 8)  |
             ((x & 0x0000000000FF0000ULL) << 24) |
             ((x & 0x000000000000FF00ULL) << 40) |
             ((x & 0x00000000000000FFULL) << 56);
    #endif
  }
  
  inline uint64_t htobe64_portable(uint64_t x) {
    return be64toh_portable(x);  // Same operation
  }
}

namespace fsx::protocol {

// FILE_OFFER_REQ payload format:
// u64 transfer_id (client-generated, network order) - optional, server can override
// u16 receiver_username_len (network order)
// bytes receiver_username
// u16 filename_len (network order)
// bytes filename
// u64 file_size (network order)
// u32 chunk_size (network order)

struct FileOfferReq {
  uint64_t client_transfer_id = 0;  // Client can suggest, server will assign
  std::string receiver_username;
  std::string filename;
  uint64_t file_size = 0;
  uint32_t chunk_size = 0;  // Recommended chunk size (server may adjust)

  static FileOfferReq deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 14) throw std::runtime_error("FILE_OFFER_REQ: payload too short");
    
    size_t pos = 0;
    FileOfferReq req;
    
    // Client transfer ID (optional, 0 if not provided)
    if (pos + 8 <= payload.size()) {
      req.client_transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
      pos += 8;
    }
    
    // Receiver username
    if (pos + 2 > payload.size()) throw std::runtime_error("FILE_OFFER_REQ: missing receiver_username_len");
    uint16_t receiver_username_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;
    if (pos + receiver_username_len > payload.size()) throw std::runtime_error("FILE_OFFER_REQ: invalid receiver_username_len");
    req.receiver_username = std::string(reinterpret_cast<const char*>(payload.data() + pos), receiver_username_len);
    pos += receiver_username_len;
    
    // Filename
    if (pos + 2 > payload.size()) throw std::runtime_error("FILE_OFFER_REQ: missing filename_len");
    uint16_t filename_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;
    if (pos + filename_len > payload.size()) throw std::runtime_error("FILE_OFFER_REQ: invalid filename_len");
    req.filename = std::string(reinterpret_cast<const char*>(payload.data() + pos), filename_len);
    pos += filename_len;
    
    // File size
    if (pos + 8 > payload.size()) throw std::runtime_error("FILE_OFFER_REQ: missing file_size");
      req.file_size = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    
    // Chunk size
    if (pos + 4 > payload.size()) throw std::runtime_error("FILE_OFFER_REQ: missing chunk_size");
    req.chunk_size = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    return req;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    // Client transfer ID
      uint64_t client_id_be = htobe64_portable(client_transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&client_id_be), 
                   reinterpret_cast<const uint8_t*>(&client_id_be) + 8);
    
    // Receiver username
    uint16_t receiver_username_len_be = htons((uint16_t)receiver_username.size());
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&receiver_username_len_be),
                   reinterpret_cast<const uint8_t*>(&receiver_username_len_be) + 2);
    payload.insert(payload.end(), receiver_username.begin(), receiver_username.end());
    
    // Filename
    uint16_t filename_len_be = htons((uint16_t)filename.size());
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&filename_len_be),
                   reinterpret_cast<const uint8_t*>(&filename_len_be) + 2);
    payload.insert(payload.end(), filename.begin(), filename.end());
    
    // File size
      uint64_t file_size_be = htobe64_portable(file_size);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&file_size_be),
                   reinterpret_cast<const uint8_t*>(&file_size_be) + 8);
    
    // Chunk size
    uint32_t chunk_size_be = htonl(chunk_size);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_size_be),
                   reinterpret_cast<const uint8_t*>(&chunk_size_be) + 4);
    
    return payload;
  }
};

// FILE_OFFER_RESP payload format:
// u8 status (0=OK, 1=FAIL)
// u64 transfer_id (server-assigned, network order)
// u16 reason_len (network order, 0 if OK)
// bytes reason (if FAIL)

struct FileOfferResp {
  bool ok = false;
  uint64_t transfer_id = 0;
  std::string reason;

  static FileOfferResp deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 9) throw std::runtime_error("FILE_OFFER_RESP: payload too short");
    
    size_t pos = 0;
    FileOfferResp resp;
    
    resp.ok = (payload[pos] == 0);
    pos += 1;
    
      resp.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    
    if (!resp.ok && pos + 2 <= payload.size()) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
      pos += 2;
      if (pos + reason_len <= payload.size()) {
        resp.reason = std::string(reinterpret_cast<const char*>(payload.data() + pos), reason_len);
      }
    }
    
    return resp;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    payload.push_back(ok ? 0 : 1);
    
      uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    if (!ok) {
      uint16_t reason_len_be = htons((uint16_t)reason.size());
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&reason_len_be),
                     reinterpret_cast<const uint8_t*>(&reason_len_be) + 2);
      payload.insert(payload.end(), reason.begin(), reason.end());
    }
    
    return payload;
  }
};

// FILE_ACCEPT_REQ payload format:
// u64 transfer_id (network order)
// u8 accept (0=reject, 1=accept)

struct FileAcceptReq {
  uint64_t transfer_id = 0;
  bool accept = false;

  static FileAcceptReq deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 9) throw std::runtime_error("FILE_ACCEPT_REQ: payload too short");
    
    FileAcceptReq req;
    req.transfer_id = be64toh(*reinterpret_cast<const uint64_t*>(payload.data()));
    req.accept = (payload[8] == 1);
    
    return req;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
      uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    payload.push_back(accept ? 1 : 0);
    
    return payload;
  }
};

// FILE_ACCEPT_RESP payload format:
// u8 status (0=OK, 1=FAIL)
// u16 reason_len (network order, 0 if OK)
// bytes reason (if FAIL)

struct FileAcceptResp {
  bool ok = false;
  std::string reason;

  static FileAcceptResp deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 1) throw std::runtime_error("FILE_ACCEPT_RESP: payload too short");
    
    FileAcceptResp resp;
    resp.ok = (payload[0] == 0);
    
    if (!resp.ok && payload.size() >= 3) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + 1));
      if (3 + reason_len <= payload.size()) {
        resp.reason = std::string(reinterpret_cast<const char*>(payload.data() + 3), reason_len);
      }
    }
    
    return resp;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    payload.push_back(ok ? 0 : 1);
    
    if (!ok) {
      uint16_t reason_len_be = htons((uint16_t)reason.size());
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&reason_len_be),
                     reinterpret_cast<const uint8_t*>(&reason_len_be) + 2);
      payload.insert(payload.end(), reason.begin(), reason.end());
    }
    
    return payload;
  }
};

// FILE_CHUNK payload format:
// u64 transfer_id (network order)
// u32 chunk_index (network order)
// bytes chunk_data (rest of payload)

struct FileChunk {
  uint64_t transfer_id = 0;
  uint32_t chunk_index = 0;
  std::vector<uint8_t> data;

  static FileChunk deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 12) throw std::runtime_error("FILE_CHUNK: payload too short");
    
    FileChunk chunk;
    chunk.transfer_id = be64toh(*reinterpret_cast<const uint64_t*>(payload.data()));
    chunk.chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + 8));
    
    if (payload.size() > 12) {
      chunk.data.assign(payload.begin() + 12, payload.end());
    }
    
    return chunk;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
      uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    uint32_t chunk_index_be = htonl(chunk_index);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                   reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
    
    payload.insert(payload.end(), data.begin(), data.end());
    
    return payload;
  }
};

// FILE_DONE payload format (Phase 7: optional file_sha256):
// u64 transfer_id (network order)
// u32 total_chunks (network order)
// u64 file_size (network order, confirmation)
// [Phase 7] optional: u8[32] file_sha256 (client-provided hash for server verification)

struct FileDone {
  uint64_t transfer_id = 0;
  uint32_t total_chunks = 0;
  uint64_t file_size = 0;
  std::vector<uint8_t> file_sha256;  // 32 bytes if sent by client; empty = not sent

  static constexpr size_t FILE_DONE_MIN_SIZE = 20;  // 8+4+8
  static constexpr size_t SHA256_SIZE = 32;

  static FileDone deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < FILE_DONE_MIN_SIZE) throw std::runtime_error("FILE_DONE: payload too short");
    
    FileDone done;
    size_t pos = 0;
    done.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    done.total_chunks = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    done.file_size = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    if (payload.size() >= pos + SHA256_SIZE) {
      done.file_sha256.assign(payload.begin() + pos, payload.begin() + pos + SHA256_SIZE);
    }
    return done;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    uint32_t total_chunks_be = htonl(total_chunks);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&total_chunks_be),
                   reinterpret_cast<const uint8_t*>(&total_chunks_be) + 4);
    
    uint64_t file_size_be = htobe64_portable(file_size);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&file_size_be),
                   reinterpret_cast<const uint8_t*>(&file_size_be) + 8);
    
    if (file_sha256.size() == SHA256_SIZE) {
      payload.insert(payload.end(), file_sha256.begin(), file_sha256.end());
    }
    return payload;
  }
};

// FILE_RESULT payload format (Phase 7: optional sha256 for UI/log):
// u64 transfer_id (network order)
// u8 status (0=OK, 1=FAIL)
// u16 path_len (network order, 0 if FAIL)
// bytes saved_path (if OK) or error_reason (if FAIL)
// [Phase 7] optional: u8 sha256_status (0=not_checked, 1=verified, 2=mismatch), then u8[32] computed_sha256

struct FileResult {
  uint64_t transfer_id = 0;
  bool ok = false;
  std::string path_or_reason;  // saved path if OK, error reason if FAIL
  // Phase 7: SHA-256 integrity for UI/log
  int sha256_status = -1;  // -1 absent, 0 not_checked (client didn't send), 1 verified, 2 mismatch
  std::vector<uint8_t> computed_sha256;  // 32 bytes when status is 1 or 2 (for UI display)

  static constexpr size_t SHA256_SIZE = 32;

  static FileResult deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 9) throw std::runtime_error("FILE_RESULT: payload too short");
    
    FileResult result;
    size_t pos = 0;
    result.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    result.ok = (payload[pos] == 0);
    pos += 1;
    if (payload.size() >= pos + 2) {
      uint16_t path_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
      pos += 2;
      if (pos + path_len <= payload.size()) {
        result.path_or_reason = std::string(reinterpret_cast<const char*>(payload.data() + pos), path_len);
        pos += path_len;
      }
      if (pos + 1 <= payload.size()) {
        result.sha256_status = static_cast<int>(payload[pos]);
        pos += 1;
        if ((result.sha256_status == 1 || result.sha256_status == 2) && pos + SHA256_SIZE <= payload.size()) {
          result.computed_sha256.assign(payload.begin() + pos, payload.begin() + pos + SHA256_SIZE);
        }
      }
    }
    return result;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    payload.push_back(ok ? 0 : 1);
    
    uint16_t path_len_be = htons(static_cast<uint16_t>(path_or_reason.size()));
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&path_len_be),
                   reinterpret_cast<const uint8_t*>(&path_len_be) + 2);
    payload.insert(payload.end(), path_or_reason.begin(), path_or_reason.end());
    
    if (sha256_status >= 0) {
      payload.push_back(static_cast<uint8_t>(sha256_status));
      if (computed_sha256.size() == SHA256_SIZE) {
        payload.insert(payload.end(), computed_sha256.begin(), computed_sha256.end());
      }
    }
    return payload;
  }
};

// ============================================================================
// Phase 4: Upload messages with CRC32
// ============================================================================

// FILE_UPLOAD_CHUNK payload format (Phase 4 + Phase 6 optional compression):
// u64 transfer_id (network order)
// u32 chunk_index (network order)
// u32 data_size (network order) — size of data on wire (compressed or raw)
// u32 crc32 (network order, CRC32 of *uncompressed* data only)
// [Phase 6] u64 original_size (network order, optional) — if present and > 0, data is zlib-compressed
// u8[data_size] data

struct FileUploadChunk {
  uint64_t transfer_id = 0;
  uint32_t chunk_index = 0;
  uint32_t data_size = 0;
  uint32_t crc32 = 0;
  uint64_t original_size = 0;  // Phase 6: uncompressed size; 0 = not compressed
  std::vector<uint8_t> data;

  static FileUploadChunk deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 20) throw std::runtime_error("FILE_UPLOAD_CHUNK: payload too short");
    
    FileUploadChunk chunk;
    size_t pos = 0;
    
    chunk.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    
    chunk.chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    chunk.data_size = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    chunk.crc32 = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    // Phase 6: optional original_size (backward compatible: old clients send 20-byte header)
    if (payload.size() >= 28 + chunk.data_size) {
      chunk.original_size = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
      pos += 8;
    }
    
    if (payload.size() < pos + chunk.data_size) {
      throw std::runtime_error("FILE_UPLOAD_CHUNK: payload size mismatch");
    }
    
    chunk.data.assign(payload.begin() + pos, payload.begin() + pos + chunk.data_size);
    
    return chunk;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    uint32_t chunk_index_be = htonl(chunk_index);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                   reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
    
    uint32_t data_size_be = htonl((uint32_t)data.size());
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&data_size_be),
                   reinterpret_cast<const uint8_t*>(&data_size_be) + 4);
    
    uint32_t crc32_be = htonl(crc32);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&crc32_be),
                   reinterpret_cast<const uint8_t*>(&crc32_be) + 4);
    
    if (original_size > 0) {
      uint64_t original_size_be = htobe64_portable(original_size);
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&original_size_be),
                     reinterpret_cast<const uint8_t*>(&original_size_be) + 8);
    }
    
    payload.insert(payload.end(), data.begin(), data.end());
    
    return payload;
  }
};

// FILE_UPLOAD_ACK payload format:
// u64 transfer_id (network order)
// u32 chunk_index (network order)

struct FileUploadAck {
  uint64_t transfer_id = 0;
  uint32_t chunk_index = 0;

  static FileUploadAck deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 12) throw std::runtime_error("FILE_UPLOAD_ACK: payload too short");
    
    FileUploadAck ack;
    ack.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data()));
    ack.chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + 8));
    
    return ack;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    uint32_t chunk_index_be = htonl(chunk_index);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                   reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
    
    return payload;
  }
};

// FILE_UPLOAD_NAK payload format:
// u64 transfer_id (network order)
// u32 chunk_index (network order)
// u32 expected_crc32 (network order, what we expected)
// u32 got_crc32 (network order, what we got)

struct FileUploadNak {
  uint64_t transfer_id = 0;
  uint32_t chunk_index = 0;
  uint32_t expected_crc32 = 0;
  uint32_t got_crc32 = 0;

  static FileUploadNak deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 20) throw std::runtime_error("FILE_UPLOAD_NAK: payload too short");
    
    FileUploadNak nak;
    size_t pos = 0;
    
    nak.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    
    nak.chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    nak.expected_crc32 = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    nak.got_crc32 = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    
    return nak;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    uint32_t chunk_index_be = htonl(chunk_index);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                   reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
    
    uint32_t expected_crc32_be = htonl(expected_crc32);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&expected_crc32_be),
                   reinterpret_cast<const uint8_t*>(&expected_crc32_be) + 4);
    
    uint32_t got_crc32_be = htonl(got_crc32);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&got_crc32_be),
                   reinterpret_cast<const uint8_t*>(&got_crc32_be) + 4);
    
    return payload;
  }
};

// ============================================================================
// Phase 4: Download messages
// ============================================================================

// FILE_DOWNLOAD_REQ payload format:
// u64 transfer_id (network order)

struct FileDownloadReq {
  uint64_t transfer_id = 0;

  static FileDownloadReq deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 8) throw std::runtime_error("FILE_DOWNLOAD_REQ: payload too short");
    
    FileDownloadReq req;
    req.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data()));
    
    return req;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    return payload;
  }
};

// FILE_DOWNLOAD_START payload format:
// u8 status (0=OK, 1=FAIL)
// u16 filename_len (network order)
// bytes filename
// u64 file_size (network order)
// u32 chunk_size (network order)
// u32 total_chunks (network order)
// u16 reason_len (network order, 0 if OK)
// bytes reason (if FAIL)

struct FileDownloadStart {
  bool ok = false;
  std::string filename;
  uint64_t file_size = 0;
  uint32_t chunk_size = 0;
  uint32_t total_chunks = 0;
  std::string reason;

  static FileDownloadStart deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 1) throw std::runtime_error("FILE_DOWNLOAD_START: payload too short");
    
    FileDownloadStart start;
    size_t pos = 0;
    
    start.ok = (payload[pos] == 0);
    pos += 1;
    
    if (start.ok) {
      if (payload.size() < pos + 2) throw std::runtime_error("FILE_DOWNLOAD_START: payload too short");
      
      uint16_t filename_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
      pos += 2;
      
      if (payload.size() < pos + filename_len) throw std::runtime_error("FILE_DOWNLOAD_START: payload too short");
      start.filename = std::string(reinterpret_cast<const char*>(payload.data() + pos), filename_len);
      pos += filename_len;
      
      if (payload.size() < pos + 8) throw std::runtime_error("FILE_DOWNLOAD_START: payload too short");
      start.file_size = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
      pos += 8;
      
      if (payload.size() < pos + 4) throw std::runtime_error("FILE_DOWNLOAD_START: payload too short");
      start.chunk_size = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
      pos += 4;
      
      if (payload.size() < pos + 4) throw std::runtime_error("FILE_DOWNLOAD_START: payload too short");
      start.total_chunks = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
      pos += 4;
    } else {
      if (payload.size() < pos + 2) throw std::runtime_error("FILE_DOWNLOAD_START: payload too short");
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
      pos += 2;
      if (payload.size() >= pos + reason_len) {
        start.reason = std::string(reinterpret_cast<const char*>(payload.data() + pos), reason_len);
      }
    }
    
    return start;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    payload.push_back(ok ? 0 : 1);
    
    if (ok) {
      uint16_t filename_len_be = htons((uint16_t)filename.size());
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&filename_len_be),
                     reinterpret_cast<const uint8_t*>(&filename_len_be) + 2);
      payload.insert(payload.end(), filename.begin(), filename.end());
      
      uint64_t file_size_be = htobe64_portable(file_size);
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&file_size_be),
                     reinterpret_cast<const uint8_t*>(&file_size_be) + 8);
      
      uint32_t chunk_size_be = htonl(chunk_size);
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_size_be),
                     reinterpret_cast<const uint8_t*>(&chunk_size_be) + 4);
      
      uint32_t total_chunks_be = htonl(total_chunks);
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&total_chunks_be),
                     reinterpret_cast<const uint8_t*>(&total_chunks_be) + 4);
    } else {
      uint16_t reason_len_be = htons((uint16_t)reason.size());
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&reason_len_be),
                     reinterpret_cast<const uint8_t*>(&reason_len_be) + 2);
      payload.insert(payload.end(), reason.begin(), reason.end());
    }
    
    return payload;
  }
};

// FILE_DOWNLOAD_CHUNK payload format:
// u64 transfer_id (network order)
// u32 chunk_index (network order)
// u32 data_size (network order)
// u32 crc32 (network order, CRC32 of data only)
// u8[data_size] data

struct FileDownloadChunk {
  uint64_t transfer_id = 0;
  uint32_t chunk_index = 0;
  uint32_t data_size = 0;
  uint32_t crc32 = 0;
  std::vector<uint8_t> data;

  static FileDownloadChunk deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 20) throw std::runtime_error("FILE_DOWNLOAD_CHUNK: payload too short");
    
    FileDownloadChunk chunk;
    size_t pos = 0;
    
    chunk.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    
    chunk.chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    chunk.data_size = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    chunk.crc32 = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    if (payload.size() < pos + chunk.data_size) {
      throw std::runtime_error("FILE_DOWNLOAD_CHUNK: payload size mismatch");
    }
    
    chunk.data.assign(payload.begin() + pos, payload.begin() + pos + chunk.data_size);
    
    return chunk;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    uint32_t chunk_index_be = htonl(chunk_index);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                   reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
    
    uint32_t data_size_be = htonl((uint32_t)data.size());
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&data_size_be),
                   reinterpret_cast<const uint8_t*>(&data_size_be) + 4);
    
    uint32_t crc32_be = htonl(crc32);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&crc32_be),
                   reinterpret_cast<const uint8_t*>(&crc32_be) + 4);
    
    payload.insert(payload.end(), data.begin(), data.end());
    
    return payload;
  }
};

// FILE_DOWNLOAD_ACK payload format:
// u64 transfer_id (network order)
// u32 chunk_index (network order)

struct FileDownloadAck {
  uint64_t transfer_id = 0;
  uint32_t chunk_index = 0;

  static FileDownloadAck deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 12) throw std::runtime_error("FILE_DOWNLOAD_ACK: payload too short");
    
    FileDownloadAck ack;
    ack.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data()));
    ack.chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + 8));
    
    return ack;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    uint32_t chunk_index_be = htonl(chunk_index);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                   reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
    
    return payload;
  }
};

// FILE_DOWNLOAD_NAK payload format:
// u64 transfer_id (network order)
// u32 chunk_index (network order)
// u32 expected_crc32 (network order, what we expected)
// u32 got_crc32 (network order, what we got)

struct FileDownloadNak {
  uint64_t transfer_id = 0;
  uint32_t chunk_index = 0;
  uint32_t expected_crc32 = 0;
  uint32_t got_crc32 = 0;

  static FileDownloadNak deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 20) throw std::runtime_error("FILE_DOWNLOAD_NAK: payload too short");
    
    FileDownloadNak nak;
    size_t pos = 0;
    
    nak.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    
    nak.chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    nak.expected_crc32 = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;
    
    nak.got_crc32 = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    
    return nak;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    uint32_t chunk_index_be = htonl(chunk_index);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                   reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
    
    uint32_t expected_crc32_be = htonl(expected_crc32);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&expected_crc32_be),
                   reinterpret_cast<const uint8_t*>(&expected_crc32_be) + 4);
    
    uint32_t got_crc32_be = htonl(got_crc32);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&got_crc32_be),
                   reinterpret_cast<const uint8_t*>(&got_crc32_be) + 4);
    
    return payload;
  }
};

// FILE_DOWNLOAD_DONE payload format:
// u64 transfer_id (network order)
// u8 status (0=OK, 1=FAIL)
// u16 reason_len (network order, 0 if OK)
// bytes reason (if FAIL)

struct FileDownloadDone {
  uint64_t transfer_id = 0;
  bool ok = false;
  std::string reason;

  static FileDownloadDone deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 9) throw std::runtime_error("FILE_DOWNLOAD_DONE: payload too short");
    
    FileDownloadDone done;
    done.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data()));
    done.ok = (payload[8] == 0);
    
    if (!done.ok && payload.size() >= 11) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + 9));
      if (11 + reason_len <= payload.size()) {
        done.reason = std::string(reinterpret_cast<const char*>(payload.data() + 11), reason_len);
      }
    }
    
    return done;
  }

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    payload.push_back(ok ? 0 : 1);
    
    if (!ok) {
      uint16_t reason_len_be = htons((uint16_t)reason.size());
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&reason_len_be),
                     reinterpret_cast<const uint8_t*>(&reason_len_be) + 2);
      payload.insert(payload.end(), reason.begin(), reason.end());
    }
    
    return payload;
  }
};

// ============================================================================
// Phase 5: Resume messages
// ============================================================================

// RESUME_QUERY payload format:
// u64 transfer_id (network order)
struct ResumeQuery {
  uint64_t transfer_id = 0;
  
  static ResumeQuery deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 8) throw std::runtime_error("RESUME_QUERY: payload too short");
    
    ResumeQuery query;
    query.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data()));
    
    return query;
  }
  
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    return payload;
  }
};

// RESUME_REPLY payload format:
// u8 can_resume (0=no, 1=yes)
// u64 transfer_id (network order)
// u16 filename_len (network order, 0 if can_resume=0)
// bytes filename (if can_resume=1)
// u64 file_size (network order, if can_resume=1)
// u32 chunk_size (network order, if can_resume=1)
// u32 last_acked_chunk_index (network order, if can_resume=1)
// u64 bytes_received (network order, if can_resume=1)
// u16 reason_len (network order, 0 if can_resume=1)
// bytes reason (if can_resume=0)
struct ResumeReply {
  bool can_resume = false;
  uint64_t transfer_id = 0;
  std::string filename;
  uint64_t file_size = 0;
  uint32_t chunk_size = 0;
  uint32_t last_acked_chunk_index = 0;
  uint64_t bytes_received = 0;
  std::string reason;  // Error reason if can_resume=false
  
  static ResumeReply deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 9) throw std::runtime_error("RESUME_REPLY: payload too short");
    
    ResumeReply reply;
    size_t pos = 0;
    
    reply.can_resume = (payload[pos] == 1);
    pos += 1;
    
    reply.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
    
    if (reply.can_resume) {
      // Parse resume info
      if (payload.size() < pos + 2) throw std::runtime_error("RESUME_REPLY: missing filename_len");
      uint16_t filename_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
      pos += 2;
      
      if (payload.size() < pos + filename_len) throw std::runtime_error("RESUME_REPLY: invalid filename_len");
      reply.filename = std::string(reinterpret_cast<const char*>(payload.data() + pos), filename_len);
      pos += filename_len;
      
      if (payload.size() < pos + 8) throw std::runtime_error("RESUME_REPLY: missing file_size");
      reply.file_size = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
      pos += 8;
      
      if (payload.size() < pos + 4) throw std::runtime_error("RESUME_REPLY: missing chunk_size");
      reply.chunk_size = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
      pos += 4;
      
      if (payload.size() < pos + 4) throw std::runtime_error("RESUME_REPLY: missing last_acked_chunk_index");
      reply.last_acked_chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
      pos += 4;
      
      if (payload.size() < pos + 8) throw std::runtime_error("RESUME_REPLY: missing bytes_received");
      reply.bytes_received = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
      pos += 8;
    } else {
      // Parse error reason
      if (payload.size() >= pos + 2) {
        uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
        pos += 2;
        if (pos + reason_len <= payload.size()) {
          reply.reason = std::string(reinterpret_cast<const char*>(payload.data() + pos), reason_len);
        }
      }
    }
    
    return reply;
  }
  
  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> payload;
    
    payload.push_back(can_resume ? 1 : 0);
    
    uint64_t transfer_id_be = htobe64_portable(transfer_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                   reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
    
    if (can_resume) {
      uint16_t filename_len_be = htons(static_cast<uint16_t>(filename.size()));
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&filename_len_be),
                     reinterpret_cast<const uint8_t*>(&filename_len_be) + 2);
      payload.insert(payload.end(), filename.begin(), filename.end());
      
      uint64_t file_size_be = htobe64_portable(file_size);
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&file_size_be),
                     reinterpret_cast<const uint8_t*>(&file_size_be) + 8);
      
      uint32_t chunk_size_be = htonl(chunk_size);
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_size_be),
                     reinterpret_cast<const uint8_t*>(&chunk_size_be) + 4);
      
      uint32_t last_acked_chunk_index_be = htonl(last_acked_chunk_index);
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&last_acked_chunk_index_be),
                     reinterpret_cast<const uint8_t*>(&last_acked_chunk_index_be) + 4);
      
      uint64_t bytes_received_be = htobe64_portable(bytes_received);
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&bytes_received_be),
                     reinterpret_cast<const uint8_t*>(&bytes_received_be) + 8);
    } else {
      uint16_t reason_len_be = htons(static_cast<uint16_t>(reason.size()));
      payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&reason_len_be),
                     reinterpret_cast<const uint8_t*>(&reason_len_be) + 2);
      payload.insert(payload.end(), reason.begin(), reason.end());
    }
    
    return payload;
  }
};

} // namespace fsx::protocol

