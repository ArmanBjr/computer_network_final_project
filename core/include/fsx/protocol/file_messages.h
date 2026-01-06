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

// FILE_DONE payload format:
// u64 transfer_id (network order)
// u32 total_chunks (network order)
// u64 file_size (network order, confirmation)

struct FileDone {
  uint64_t transfer_id = 0;
  uint32_t total_chunks = 0;
  uint64_t file_size = 0;

  static FileDone deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 16) throw std::runtime_error("FILE_DONE: payload too short");
    
    FileDone done;
    done.transfer_id = be64toh(*reinterpret_cast<const uint64_t*>(payload.data()));
    done.total_chunks = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + 8));
    done.file_size = be64toh(*reinterpret_cast<const uint64_t*>(payload.data() + 12));
    
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
    
    return payload;
  }
};

// FILE_RESULT payload format:
// u64 transfer_id (network order)
// u8 status (0=OK, 1=FAIL)
// u16 path_len (network order, 0 if FAIL)
// bytes saved_path (if OK) or error_reason (if FAIL)

struct FileResult {
  uint64_t transfer_id = 0;
  bool ok = false;
  std::string path_or_reason;  // saved path if OK, error reason if FAIL

  static FileResult deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 9) throw std::runtime_error("FILE_RESULT: payload too short");
    
    FileResult result;
    result.transfer_id = be64toh(*reinterpret_cast<const uint64_t*>(payload.data()));
    result.ok = (payload[8] == 0);
    
    if (payload.size() >= 11) {
      uint16_t path_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + 9));
      if (11 + path_len <= payload.size()) {
        result.path_or_reason = std::string(reinterpret_cast<const char*>(payload.data() + 11), path_len);
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
    
    uint16_t path_len_be = htons((uint16_t)path_or_reason.size());
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&path_len_be),
                   reinterpret_cast<const uint8_t*>(&path_len_be) + 2);
    payload.insert(payload.end(), path_or_reason.begin(), path_or_reason.end());
    
    return payload;
  }
};

} // namespace fsx::protocol

