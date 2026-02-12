// File Transfer Test Client (Phase 4-9)
// Usage:
//   send: ./test_file_transfer send <username> <password> <receiver_username> <filepath> [host] [port] [--compress] [--corrupt-upload-once N]
//   recv: ./test_file_transfer recv <username> <password> <transfer_id> <output_path> [host] [port]
//   resume-send: ./test_file_transfer resume-send <username> <password> <transfer_id> <filepath> [host] [port]
//   set-throttle: ./test_file_transfer set-throttle <scope:global|user> <bps> [user_id] [host] [port]
//   list-transfers: ./test_file_transfer list-transfers [host] [port]

#include <boost/asio.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <arpa/inet.h>
#include <thread>
#include <chrono>
#include <iomanip>
#include "fsx/transfer/integrity.h"
#include "fsx/transfer/zlib_codec.h"
#include "fsx/crypto/rsa.h"
#include "fsx/crypto/aes_gcm.h"

static constexpr uint32_t MAGIC = 0x46535831; // FSX1
static constexpr uint8_t VERSION = 1;
static constexpr uint32_t DEFAULT_CHUNK_SIZE = 256 * 1024; // 256KB

#pragma pack(push, 1)
struct Header {
  uint32_t magic_be;
  uint8_t  version;
  uint8_t  type;
  uint32_t len_be;
  uint16_t reserved_be;
};
#pragma pack(pop)

// Portable 64-bit endian conversion
inline uint64_t be64toh_portable(uint64_t x) {
  #if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(x);
  #elif defined(_WIN32) || defined(_WIN64)
    return _byteswap_uint64(x);
  #else
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
  return be64toh_portable(x);
}

// Forward declarations for helpers used in resume path
static Header read_header(boost::asio::ip::tcp::socket& sock);
static std::vector<uint8_t> read_payload(boost::asio::ip::tcp::socket& sock, uint32_t len);

struct ResumeReplyInfo {
  bool can_resume = false;
  uint64_t transfer_id = 0;
  std::string filename;
  uint64_t file_size = 0;
  uint32_t chunk_size = 0;
  uint32_t last_acked_chunk_index = 0;
  uint64_t bytes_received = 0;
  std::string reason;
};

static std::vector<uint8_t> make_frame(uint8_t type, const std::vector<uint8_t>& payload) {
  Header h{};
  h.magic_be = htonl(MAGIC);
  h.version = VERSION;
  h.type = type;
  h.len_be = htonl(static_cast<uint32_t>(payload.size()));
  h.reserved_be = htons(0);

  std::vector<uint8_t> out(sizeof(h) + payload.size());
  std::memcpy(out.data(), &h, sizeof(h));
  if (!payload.empty()) {
    std::memcpy(out.data() + sizeof(h), payload.data(), payload.size());
  }
  return out;
}

static std::vector<uint8_t> make_login_req(const std::string& username, const std::string& password) {
  std::vector<uint8_t> payload;
  payload.reserve(4 + username.size() + password.size());
  
  uint16_t u_len = htons(static_cast<uint16_t>(username.size()));
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&u_len),
                 reinterpret_cast<const uint8_t*>(&u_len) + 2);
  payload.insert(payload.end(), username.begin(), username.end());
  
  uint16_t p_len = htons(static_cast<uint16_t>(password.size()));
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&p_len),
                 reinterpret_cast<const uint8_t*>(&p_len) + 2);
  payload.insert(payload.end(), password.begin(), password.end());
  
  return make_frame(12, payload); // LOGIN_REQ = 12
}

static std::vector<uint8_t> make_file_offer_req(const std::string& receiver_username,
                                                 const std::string& filename,
                                                 uint64_t file_size,
                                                 uint32_t chunk_size) {
  std::vector<uint8_t> payload;
  
  // Client transfer ID (0 = server assigns)
  uint64_t client_id_be = htobe64_portable(0);
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&client_id_be),
                 reinterpret_cast<const uint8_t*>(&client_id_be) + 8);
  
  // Receiver username
  uint16_t receiver_username_len_be = htons(static_cast<uint16_t>(receiver_username.size()));
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&receiver_username_len_be),
                 reinterpret_cast<const uint8_t*>(&receiver_username_len_be) + 2);
  payload.insert(payload.end(), receiver_username.begin(), receiver_username.end());
  
  // Filename
  uint16_t filename_len_be = htons(static_cast<uint16_t>(filename.size()));
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
  
  return make_frame(30, payload); // FILE_OFFER_REQ = 30
}

static std::vector<uint8_t> make_file_accept_req(uint64_t transfer_id, bool accept) {
  std::vector<uint8_t> payload;
  
  uint64_t transfer_id_be = htobe64_portable(transfer_id);
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                 reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
  
  payload.push_back(accept ? 1 : 0);
  
  return make_frame(32, payload); // FILE_ACCEPT_REQ = 32
}

static std::vector<uint8_t> make_file_chunk(uint64_t transfer_id, uint32_t chunk_index,
                                             const std::vector<uint8_t>& data) {
  std::vector<uint8_t> payload;
  
  uint64_t transfer_id_be = htobe64_portable(transfer_id);
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                 reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
  
  uint32_t chunk_index_be = htonl(chunk_index);
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                 reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
  
  payload.insert(payload.end(), data.begin(), data.end());
  
  return make_frame(34, payload); // FILE_CHUNK = 34
}

// Phase 4: FILE_UPLOAD_CHUNK with CRC32. Phase 6: optional original_size for compressed data.
static std::vector<uint8_t> make_file_upload_chunk(uint64_t transfer_id, uint32_t chunk_index,
                                                     const std::vector<uint8_t>& data, uint32_t crc32,
                                                     uint64_t original_size = 0) {
  std::vector<uint8_t> payload;
  
  uint64_t transfer_id_be = htobe64_portable(transfer_id);
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                 reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);
  
  uint32_t chunk_index_be = htonl(chunk_index);
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&chunk_index_be),
                 reinterpret_cast<const uint8_t*>(&chunk_index_be) + 4);
  
  uint32_t data_size_be = htonl(static_cast<uint32_t>(data.size()));
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
  
  return make_frame(37, payload); // FILE_UPLOAD_CHUNK = 37
}

static std::vector<uint8_t> make_file_done(uint64_t transfer_id, uint32_t total_chunks, uint64_t file_size) {
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
  
  return make_frame(35, payload); // FILE_DONE = 35
}

// Phase 5: Resume query (RESUME_QUERY / RESUME_REPLY)
static std::vector<uint8_t> make_resume_query(uint64_t transfer_id) {
  std::vector<uint8_t> payload;

  uint64_t transfer_id_be = htobe64_portable(transfer_id);
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&transfer_id_be),
                 reinterpret_cast<const uint8_t*>(&transfer_id_be) + 8);

  return make_frame(50, payload); // RESUME_QUERY = 50
}

static ResumeReplyInfo read_resume_reply(boost::asio::ip::tcp::socket& sock) {
  Header h = read_header(sock);
  if (h.type != 51) { // RESUME_REPLY
    throw std::runtime_error("Expected RESUME_REPLY (51), got type " + std::to_string(h.type));
  }

  auto payload = read_payload(sock, ntohl(h.len_be));
  if (payload.size() < 9) {
    throw std::runtime_error("RESUME_REPLY: payload too short");
  }

  ResumeReplyInfo info;
  size_t pos = 0;

  info.can_resume = (payload[pos] == 1);
  pos += 1;

  info.transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
  pos += 8;

  if (info.can_resume) {
    if (payload.size() < pos + 2) {
      throw std::runtime_error("RESUME_REPLY: missing filename_len");
    }
    uint16_t filename_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;

    if (payload.size() < pos + filename_len) {
      throw std::runtime_error("RESUME_REPLY: invalid filename_len");
    }
    info.filename = std::string(reinterpret_cast<const char*>(payload.data() + pos), filename_len);
    pos += filename_len;

    if (payload.size() < pos + 8) {
      throw std::runtime_error("RESUME_REPLY: missing file_size");
    }
    info.file_size = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;

    if (payload.size() < pos + 4) {
      throw std::runtime_error("RESUME_REPLY: missing chunk_size");
    }
    info.chunk_size = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;

    if (payload.size() < pos + 4) {
      throw std::runtime_error("RESUME_REPLY: missing last_acked_chunk_index");
    }
    info.last_acked_chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + pos));
    pos += 4;

    if (payload.size() < pos + 8) {
      throw std::runtime_error("RESUME_REPLY: missing bytes_received");
    }
    info.bytes_received = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;
  } else {
    // Parse error reason (optional)
    if (payload.size() >= pos + 2) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
      pos += 2;
      if (payload.size() >= pos + reason_len && reason_len > 0) {
        info.reason = std::string(reinterpret_cast<const char*>(payload.data() + pos), reason_len);
      }
    }
  }

  return info;
}

static Header read_header(boost::asio::ip::tcp::socket& sock) {
  Header h{};
  boost::asio::read(sock, boost::asio::buffer(&h, sizeof(h)));
  
  if (ntohl(h.magic_be) != MAGIC) {
    throw std::runtime_error("bad magic");
  }
  if (h.version != VERSION) {
    throw std::runtime_error("bad version");
  }
  
  return h;
}

static std::vector<uint8_t> read_payload(boost::asio::ip::tcp::socket& sock, uint32_t len) {
  std::vector<uint8_t> payload(len);
  if (len > 0) {
    boost::asio::read(sock, boost::asio::buffer(payload));
  }
  return payload;
}

// Phase 4: Read ACK/NAK (blocking, no timeout - timeout handled by retry logic)
static bool read_upload_ack_nak(boost::asio::ip::tcp::socket& sock, 
                                 uint64_t expected_transfer_id,
                                 uint32_t expected_chunk_index) {
  Header h = read_header(sock);
  
  if (h.type == 38) { // FILE_UPLOAD_ACK
    auto payload = read_payload(sock, ntohl(h.len_be));
    if (payload.size() < 12) {
      std::cerr << "[SEND] FILE_UPLOAD_ACK too short\n";
      return false;
    }
    uint64_t ack_transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data()));
    uint32_t ack_chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + 8));
    
    if (ack_transfer_id == expected_transfer_id && ack_chunk_index == expected_chunk_index) {
      std::cout << "[SEND] ACK received for chunk " << expected_chunk_index << "\n";
      std::cout.flush();
      return true;
    } else {
      std::cerr << "[SEND] ACK mismatch: expected transfer_id=" << expected_transfer_id 
                << " chunk_index=" << expected_chunk_index 
                << " got transfer_id=" << ack_transfer_id 
                << " chunk_index=" << ack_chunk_index << "\n";
      return false;
    }
  } else if (h.type == 39) { // FILE_UPLOAD_NAK
    auto payload = read_payload(sock, ntohl(h.len_be));
    if (payload.size() < 20) {
      std::cerr << "[SEND] FILE_UPLOAD_NAK too short\n";
      return false;
    }
    uint64_t nak_transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data()));
    uint32_t nak_chunk_index = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + 8));
    uint32_t expected_crc = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + 12));
    uint32_t got_crc = ntohl(*reinterpret_cast<const uint32_t*>(payload.data() + 16));
    
    std::cout << "[SEND] NAK received for chunk " << nak_chunk_index 
              << " expected_crc=0x" << std::hex << expected_crc 
              << " got_crc=0x" << got_crc << std::dec << "\n";
    std::cout.flush();
    return false;
  } else {
    std::cerr << "[SEND] Unexpected message type " << (int)h.type << " (expected ACK=38 or NAK=39), ignoring\n";
    return false;
  }
}

// Phase 8: Consume KEY_EXCHANGE_PUBKEY (60), send KEY_EXCHANGE_SESSION_KEY (61), store session key for encryption.
static bool maybe_consume_key_exchange_pubkey(boost::asio::ip::tcp::socket& sock,
                                               std::vector<uint8_t>* out_session_key) {
  auto h = read_header(sock);
  if (h.type != 60) { // KEY_EXCHANGE_PUBKEY
    std::cerr << "[ERROR] Expected KEY_EXCHANGE_PUBKEY (60), got type " << (int)h.type << "\n";
    return false;
  }
  auto pub_der = read_payload(sock, ntohl(h.len_be));
  std::cout << "[LOGIN] Consumed KEY_EXCHANGE_PUBKEY (" << pub_der.size() << " bytes)\n";

  if (out_session_key && !pub_der.empty()) {
    fsx::crypto::RsaPublicKey client_pub;
    if (!client_pub.load_from_der(pub_der)) {
      std::cerr << "[ERROR] Failed to load server public key\n";
      return false;
    }
    uint8_t key[fsx::crypto::AES256_KEY_SIZE];
    fsx::crypto::AesGcm::random_key(key);
    auto encrypted_key = client_pub.encrypt(key, fsx::crypto::AES256_KEY_SIZE);
    if (encrypted_key.empty()) {
      std::cerr << "[ERROR] Failed to encrypt session key\n";
      return false;
    }
    auto frame = make_frame(61, encrypted_key); // KEY_EXCHANGE_SESSION_KEY = 61
    boost::asio::write(sock, boost::asio::buffer(frame));
    out_session_key->assign(key, key + fsx::crypto::AES256_KEY_SIZE);
    std::cout << "[LOGIN] Sent KEY_EXCHANGE_SESSION_KEY (encrypted 32-byte key)\n";
  }
  return true;
}

static bool do_login(boost::asio::ip::tcp::socket& sock, const std::string& username, const std::string& password,
                     std::vector<uint8_t>* out_session_key = nullptr) {
  if (!maybe_consume_key_exchange_pubkey(sock, out_session_key)) return false;

  std::cout << "[LOGIN] Sending LOGIN_REQ for " << username << "\n";
  auto frame = make_login_req(username, password);
  boost::asio::write(sock, boost::asio::buffer(frame));

  auto h = read_header(sock);
  if (h.type != 13) { // LOGIN_RESP
    std::cerr << "[ERROR] Expected LOGIN_RESP, got type " << (int)h.type << "\n";
    return false;
  }

  auto payload = read_payload(sock, ntohl(h.len_be));
  if (payload.size() < 1) {
    std::cerr << "[ERROR] LOGIN_RESP too short\n";
    return false;
  }

  bool ok = payload[0] != 0;
  if (ok) {
    std::cout << "[LOGIN] Success\n";
  } else {
    std::cerr << "[LOGIN] Failed\n";
  }
  return ok;
}

static void do_send(boost::asio::ip::tcp::socket& sock,
                    const std::string& receiver_username,
                    const std::string& filepath,
                    int corrupt_chunk_index = -1,
                    bool use_compress = false,
                    const std::vector<uint8_t>* session_key = nullptr) {
  // Check file exists
  if (!std::filesystem::exists(filepath)) {
    throw std::runtime_error("File not found: " + filepath);
  }
  
  auto file_size = std::filesystem::file_size(filepath);
  auto filename = std::filesystem::path(filepath).filename().string();
  
  std::cout << "[SEND] File: " << filename << " (" << file_size << " bytes)\n";
  std::cout << "[SEND] Receiver: " << receiver_username << (use_compress ? " [compress=on]" : "") << "\n";
  
  // Open file
  std::ifstream file(filepath, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + filepath);
  }
  
  // Send FILE_OFFER_REQ
  std::cout << "[SEND] Sending FILE_OFFER_REQ...\n";
  auto offer_frame = make_file_offer_req(receiver_username, filename, file_size, DEFAULT_CHUNK_SIZE);
  boost::asio::write(sock, boost::asio::buffer(offer_frame));
  
  // Read FILE_OFFER_RESP
  auto h = read_header(sock);
  if (h.type != 31) { // FILE_OFFER_RESP
    throw std::runtime_error("Expected FILE_OFFER_RESP, got type " + std::to_string(h.type));
  }
  
  auto resp_payload = read_payload(sock, ntohl(h.len_be));
  if (resp_payload.size() < 9) {
    throw std::runtime_error("FILE_OFFER_RESP too short");
  }
  
  bool ok = resp_payload[0] == 0;
  uint64_t transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(resp_payload.data() + 1));
  
  if (!ok) {
    std::string reason;
    if (resp_payload.size() >= 11) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(resp_payload.data() + 9));
      if (11 + reason_len <= resp_payload.size()) {
        reason = std::string(reinterpret_cast<const char*>(resp_payload.data() + 11), reason_len);
      }
    }
    throw std::runtime_error("FILE_OFFER failed: " + reason);
  }
  
  std::cout << "[SEND] Transfer ID: " << transfer_id << "\n";
  std::cout.flush();
  std::cout << "[SEND] >>> Receiver should run: recv <receiver_username> <receiver_password> " << transfer_id << " <output_path>\n";
  std::cout.flush();
  std::cout << "[SEND] Waiting for receiver to accept...\n";
  std::cout.flush();
  
  // Wait for FILE_ACCEPT_RESP
  h = read_header(sock);
  if (h.type != 33) { // FILE_ACCEPT_RESP
    throw std::runtime_error("Expected FILE_ACCEPT_RESP, got type " + std::to_string(h.type));
  }
  
  auto accept_payload = read_payload(sock, ntohl(h.len_be));
  if (accept_payload.size() < 1) {
    throw std::runtime_error("FILE_ACCEPT_RESP too short");
  }
  
  bool accepted = accept_payload[0] == 0;
  if (!accepted) {
    std::string reason;
    if (accept_payload.size() >= 3) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(accept_payload.data() + 1));
      if (3 + reason_len <= accept_payload.size()) {
        reason = std::string(reinterpret_cast<const char*>(accept_payload.data() + 3), reason_len);
      }
    }
    throw std::runtime_error("Receiver rejected: " + reason);
  }
  
  std::cout << "[SEND] Accepted! Sending chunks...\n";
  std::cout.flush();
  
  // Send chunks
  std::vector<uint8_t> chunk_data(DEFAULT_CHUNK_SIZE);
  uint32_t chunk_index = 0;
  uint64_t total_sent = 0;
  
  // Reset file to beginning (in case it was read before)
  file.clear();
  file.seekg(0, std::ios::beg);
  
  std::cout << "[SEND] Starting chunk loop (file_size=" << file_size << ", chunk_size=" << DEFAULT_CHUNK_SIZE << ")...\n";
  std::cout.flush();
  
  // Phase 9: timing for speed measurement
  auto transfer_start = std::chrono::steady_clock::now();

  // Read and send chunks (Phase 4: with CRC32 + stop-and-wait + retry)
  constexpr int MAX_RETRIES = 5;
  while (total_sent < file_size) {
    size_t remaining = file_size - total_sent;
    size_t to_read = (remaining < DEFAULT_CHUNK_SIZE) ? remaining : DEFAULT_CHUNK_SIZE;
    
    chunk_data.resize(to_read);
    file.read(reinterpret_cast<char*>(chunk_data.data()), to_read);
    size_t bytes_read = file.gcount();
    
    if (bytes_read == 0) {
      std::cout << "[SEND] Warning: read 0 bytes, breaking loop (total_sent=" << total_sent << ", file_size=" << file_size << ")\n";
      std::cout.flush();
      break;
    }
    
    if (bytes_read != to_read) {
      std::cout << "[SEND] Warning: read " << bytes_read << " bytes, expected " << to_read << "\n";
      std::cout.flush();
      chunk_data.resize(bytes_read);
    }
    
    // Phase 4: CRC32 of *uncompressed* chunk (used for integrity on both sides)
    uint32_t chunk_crc32 = fsx::transfer::IntegrityService::crc32(chunk_data);
    
    // Phase 6: Optionally compress; data we send and original_size for protocol
    std::vector<uint8_t> data_to_send = chunk_data;
    uint64_t original_size = 0;
    if (use_compress) {
      std::vector<uint8_t> compressed = fsx::transfer::ZlibCodec::compress(chunk_data);
      if (!compressed.empty() && compressed.size() < chunk_data.size()) {
        data_to_send = std::move(compressed);
        original_size = static_cast<uint64_t>(chunk_data.size());
      }
    }
    
    // Phase 4: Fault injection for testing NAK (only on first attempt)
    bool is_corrupted = (corrupt_chunk_index >= 0 && chunk_index == static_cast<uint32_t>(corrupt_chunk_index));
    uint32_t sent_crc32 = chunk_crc32;
    if (is_corrupted) {
      sent_crc32 = chunk_crc32 ^ 0xFFFFFFFF;
      std::cout << "[SEND] FAULT INJECTION: Corrupting CRC32 for chunk " << chunk_index 
                << " (correct=0x" << std::hex << chunk_crc32 
                << ", corrupted=0x" << sent_crc32 << std::dec << ")\n";
      std::cout.flush();
    }
    
    bool chunk_acked = false;
    int retry_count = 0;
    
    while (!chunk_acked && retry_count < MAX_RETRIES) {
      if (retry_count > 0) {
        std::cout << "[SEND] Retry " << retry_count << " for chunk " << chunk_index << "\n";
        std::cout.flush();
        sent_crc32 = chunk_crc32;
      } else {
        std::cout << "[SEND] Sending chunk " << chunk_index << " (" << bytes_read << " bytes"
                  << (original_size ? " compressed->" + std::to_string(data_to_send.size()) : "")
                  << ", crc32=0x" << std::hex << sent_crc32 << std::dec << ")...\n";
        std::cout.flush();
      }
      
      auto chunk_frame = make_file_upload_chunk(transfer_id, chunk_index, data_to_send, sent_crc32, original_size);
      if (session_key && session_key->size() == fsx::crypto::AES256_KEY_SIZE) {
        // Phase 8: encrypt chunk payload (bytes after 12-byte header) with AES-GCM
        const size_t payload_offset = 12;
        if (chunk_frame.size() > payload_offset) {
          auto encrypted = fsx::crypto::AesGcm::encrypt(
            chunk_frame.data() + payload_offset, chunk_frame.size() - payload_offset,
            session_key->data(), nullptr);
          auto enc_frame = make_frame(62, encrypted); // FILE_UPLOAD_CHUNK_ENCRYPTED = 62
          boost::asio::write(sock, boost::asio::buffer(enc_frame));
        } else {
          boost::asio::write(sock, boost::asio::buffer(chunk_frame));
        }
      } else {
        boost::asio::write(sock, boost::asio::buffer(chunk_frame));
      }
      
      // Phase 4: Wait for ACK/NAK (stop-and-wait)
      bool ack_received = read_upload_ack_nak(sock, transfer_id, chunk_index);
      
      if (ack_received) {
        chunk_acked = true;
        total_sent += bytes_read;
        // Phase 9: speed measurement
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - transfer_start).count();
        double speed_kbs = elapsed > 0.01 ? (total_sent / 1024.0) / elapsed : 0;
        std::cout << "[SEND] Chunk " << chunk_index << " ACKed: " << bytes_read
                  << " bytes (total: " << total_sent << "/" << file_size
                  << ", speed: " << std::fixed << std::setprecision(1)
                  << speed_kbs << " KB/s)\n";
        std::cout.flush();
      } else {
        retry_count++;
        if (retry_count >= MAX_RETRIES) {
          throw std::runtime_error("Failed to send chunk " + std::to_string(chunk_index) + " after " + std::to_string(MAX_RETRIES) + " retries");
        }
        // Will retry in next iteration
      }
    }
    
    chunk_index++;
    
    if (bytes_read < to_read) {
      // EOF reached
      break;
    }
  }
  
  // Check if we read anything
  if (chunk_index == 0 && file_size > 0) {
    std::cerr << "[SEND] ERROR: No chunks sent but file_size > 0! File may not have been read correctly.\n";
    std::cerr.flush();
  }
  
  std::cout << "[SEND] Chunk loop finished. Total chunks sent: " << chunk_index << ", total bytes: " << total_sent << "/" << file_size << "\n";
  std::cout.flush();
  
  // Calculate total_chunks correctly: ceil(file_size / chunk_size)
  // If file_size is 0, total_chunks should be 0
  // Otherwise, at least 1 chunk (even if file_size < chunk_size)
  uint32_t total_chunks = 0;
  if (file_size > 0) {
    total_chunks = (file_size + DEFAULT_CHUNK_SIZE - 1) / DEFAULT_CHUNK_SIZE; // Ceiling division
    // Ensure we sent at least as many chunks as calculated
    if (chunk_index == 0 && file_size > 0) {
      // Edge case: file was read but loop didn't execute (shouldn't happen, but be safe)
      total_chunks = 1;
    } else if (chunk_index > total_chunks) {
      // If we sent more chunks than expected, use actual count
      total_chunks = chunk_index;
    }
  }
  std::cout << "[SEND] Sending FILE_DONE (total_chunks=" << total_chunks << ")\n";
  auto done_frame = make_file_done(transfer_id, total_chunks, file_size);
  boost::asio::write(sock, boost::asio::buffer(done_frame));
  
  // Wait for FILE_RESULT
  h = read_header(sock);
  if (h.type != 36) { // FILE_RESULT
    throw std::runtime_error("Expected FILE_RESULT, got type " + std::to_string(h.type));
  }
  
  auto result_payload = read_payload(sock, ntohl(h.len_be));
  if (result_payload.size() < 9) {
    throw std::runtime_error("FILE_RESULT too short");
  }
  
  uint64_t result_transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(result_payload.data()));
  bool result_ok = result_payload[8] == 0;
  
  if (result_ok) {
    std::string path;
    if (result_payload.size() >= 11) {
      uint16_t path_len = ntohs(*reinterpret_cast<const uint16_t*>(result_payload.data() + 9));
      if (11 + path_len <= result_payload.size()) {
        path = std::string(reinterpret_cast<const char*>(result_payload.data() + 11), path_len);
      }
    }
    // Phase 9: print average speed
    auto transfer_end = std::chrono::steady_clock::now();
    double total_elapsed = std::chrono::duration<double>(transfer_end - transfer_start).count();
    double avg_speed = total_elapsed > 0.01 ? (file_size / 1024.0) / total_elapsed : 0;
    std::cout << "[SEND] SUCCESS! File saved at: " << path << "\n";
    std::cout << "[SEND] Average speed: " << std::fixed << std::setprecision(1) << avg_speed << " KB/s"
              << " (elapsed: " << std::setprecision(2) << total_elapsed << "s)\n";
  } else {
    std::string reason;
    if (result_payload.size() >= 11) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(result_payload.data() + 9));
      if (11 + reason_len <= result_payload.size()) {
        reason = std::string(reinterpret_cast<const char*>(result_payload.data() + 11), reason_len);
      }
    }
    throw std::runtime_error("FILE_RESULT failed: " + reason);
  }
}

// Phase 5: Resume upload from last ACKed chunk using RESUME_QUERY
static void do_resume_send(boost::asio::ip::tcp::socket& sock,
                           uint64_t transfer_id,
                           const std::string& filepath) {
  std::cout << "[RESUME] Requesting resume for transfer_id=" << transfer_id << "\n";
  std::cout.flush();

  // Send RESUME_QUERY
  auto query_frame = make_resume_query(transfer_id);
  boost::asio::write(sock, boost::asio::buffer(query_frame));

  // Read RESUME_REPLY
  ResumeReplyInfo reply = read_resume_reply(sock);

  if (!reply.can_resume) {
    std::cout << "[RESUME] Cannot resume transfer_id=" << transfer_id << "\n";
    if (!reply.reason.empty()) {
      std::cout << "[RESUME] Reason: " << reply.reason << "\n";
    }
    std::cout.flush();
    throw std::runtime_error("Server reported that transfer cannot be resumed");
  }

  std::cout << "[RESUME] Server allows resume\n";
  std::cout << "[RESUME] Filename: " << reply.filename << "\n";
  std::cout << "[RESUME] File size: " << reply.file_size << " bytes\n";
  std::cout << "[RESUME] Chunk size: " << reply.chunk_size << " bytes\n";
  std::cout << "[RESUME] Last ACKed chunk index: " << reply.last_acked_chunk_index << "\n";
  std::cout << "[RESUME] Bytes already received on server: " << reply.bytes_received << "\n";
  std::cout.flush();

  // Open local file and verify size
  if (!std::filesystem::exists(filepath)) {
    throw std::runtime_error("Local file for resume not found: " + filepath);
  }

  auto local_size = std::filesystem::file_size(filepath);
  if (local_size != reply.file_size) {
    throw std::runtime_error("Local file size (" + std::to_string(local_size) +
                             ") does not match server file_size (" + std::to_string(reply.file_size) + ")");
  }

  std::ifstream file(filepath, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open local file for resume: " + filepath);
  }

  // Seek to the resume offset
  uint64_t resume_offset = reply.bytes_received;
  std::cout << "[RESUME] Seeking local file to offset " << resume_offset << "\n";
  std::cout.flush();

  if (resume_offset > 0) {
    file.seekg(static_cast<std::streamoff>(resume_offset), std::ios::beg);
    if (!file) {
      throw std::runtime_error("Failed to seek to resume offset in local file");
    }
  }

  uint32_t chunk_size = reply.chunk_size > 0 ? reply.chunk_size : DEFAULT_CHUNK_SIZE;

  std::vector<uint8_t> chunk_data(chunk_size);
  uint32_t chunk_index = reply.last_acked_chunk_index + 1;
  uint64_t total_sent = resume_offset;

  std::cout << "[RESUME] Starting upload from chunk_index=" << chunk_index
            << " (offset=" << resume_offset << ")\n";
  std::cout.flush();

  constexpr int MAX_RETRIES = 5;

  while (total_sent < reply.file_size) {
    size_t remaining = reply.file_size - total_sent;
    size_t to_read = (remaining < chunk_size) ? remaining : chunk_size;

    chunk_data.resize(to_read);
    file.read(reinterpret_cast<char*>(chunk_data.data()), to_read);
    size_t bytes_read = static_cast<size_t>(file.gcount());

    if (bytes_read == 0) {
      std::cout << "[RESUME] Warning: read 0 bytes while total_sent=" << total_sent
                << " file_size=" << reply.file_size << "\n";
      std::cout.flush();
      break;
    }

    if (bytes_read != to_read) {
      std::cout << "[RESUME] Warning: read " << bytes_read << " bytes, expected " << to_read << "\n";
      std::cout.flush();
      chunk_data.resize(bytes_read);
    }

    uint32_t chunk_crc32 = fsx::transfer::IntegrityService::crc32(chunk_data);

    bool chunk_acked = false;
    int retry_count = 0;

    while (!chunk_acked && retry_count < MAX_RETRIES) {
      if (retry_count > 0) {
        std::cout << "[RESUME] Retry " << retry_count << " for chunk " << chunk_index << "\n";
      } else {
        std::cout << "[RESUME] Sending resumed chunk " << chunk_index
                  << " (" << bytes_read << " bytes, crc32=0x"
                  << std::hex << chunk_crc32 << std::dec << ")\n";
      }
      std::cout.flush();

      auto chunk_frame = make_file_upload_chunk(transfer_id, chunk_index, chunk_data, chunk_crc32);
      boost::asio::write(sock, boost::asio::buffer(chunk_frame));

      bool ack_received = read_upload_ack_nak(sock, transfer_id, chunk_index);
      if (ack_received) {
        chunk_acked = true;
        total_sent += bytes_read;
        std::cout << "[RESUME] Chunk " << chunk_index << " ACKed: " << bytes_read
                  << " bytes (total: " << total_sent << "/" << reply.file_size << ")\n";
        std::cout.flush();
      } else {
        retry_count++;
        if (retry_count >= MAX_RETRIES) {
          throw std::runtime_error("Failed to send resumed chunk " + std::to_string(chunk_index) +
                                   " after " + std::to_string(MAX_RETRIES) + " retries");
        }
      }
    }

    chunk_index++;

    if (bytes_read < to_read) {
      break;
    }
  }

  std::cout << "[RESUME] Finished resumed upload. total_sent=" << total_sent
            << " / " << reply.file_size << " bytes\n";
  std::cout.flush();

  // Reuse FILE_DONE path to finalize file
  uint32_t total_chunks = 0;
  if (reply.file_size > 0) {
    total_chunks = (reply.file_size + chunk_size - 1) / chunk_size;
  }

  std::cout << "[RESUME] Sending FILE_DONE (total_chunks=" << total_chunks << ")\n";
  auto done_frame = make_file_done(transfer_id, total_chunks, reply.file_size);
  boost::asio::write(sock, boost::asio::buffer(done_frame));

  Header h = read_header(sock);
  if (h.type != 36) { // FILE_RESULT
    throw std::runtime_error("Expected FILE_RESULT after resume, got type " + std::to_string(h.type));
  }

  auto result_payload = read_payload(sock, ntohl(h.len_be));
  if (result_payload.size() < 9) {
    throw std::runtime_error("FILE_RESULT after resume too short");
  }

  uint64_t result_transfer_id = be64toh_portable(*reinterpret_cast<const uint64_t*>(result_payload.data()));
  bool result_ok = result_payload[8] == 0;

  if (!result_ok) {
    std::string reason;
    if (result_payload.size() >= 11) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(result_payload.data() + 9));
      if (11 + reason_len <= result_payload.size()) {
        reason = std::string(reinterpret_cast<const char*>(result_payload.data() + 11), reason_len);
      }
    }
    throw std::runtime_error("FILE_RESULT after resume failed: " + reason);
  }

  std::string path;
  if (result_payload.size() >= 11) {
    uint16_t path_len = ntohs(*reinterpret_cast<const uint16_t*>(result_payload.data() + 9));
    if (11 + path_len <= result_payload.size()) {
      path = std::string(reinterpret_cast<const char*>(result_payload.data() + 11), path_len);
    }
  }

  std::cout << "[RESUME] SUCCESS! File saved at: " << path << "\n";
  std::cout.flush();
}
static void do_recv(boost::asio::ip::tcp::socket& sock, uint64_t transfer_id, const std::string& output_path) {
  std::cout << "[RECV] Accepting transfer_id=" << transfer_id << "\n";
  std::cout << "[RECV] Output path: " << output_path << "\n";
  
  // Send FILE_ACCEPT_REQ
  std::cout << "[RECV] Sending FILE_ACCEPT_REQ (accept=true)...\n";
  auto accept_frame = make_file_accept_req(transfer_id, true);
  boost::asio::write(sock, boost::asio::buffer(accept_frame));
  
  // Read FILE_ACCEPT_RESP
  auto h = read_header(sock);
  if (h.type != 33) { // FILE_ACCEPT_RESP
    throw std::runtime_error("Expected FILE_ACCEPT_RESP, got type " + std::to_string(h.type));
  }
  
  auto accept_payload = read_payload(sock, ntohl(h.len_be));
  if (accept_payload.size() < 1) {
    throw std::runtime_error("FILE_ACCEPT_RESP too short");
  }
  
  bool accepted = accept_payload[0] == 0;
  if (!accepted) {
    std::string reason;
    if (accept_payload.size() >= 3) {
      uint16_t reason_len = ntohs(*reinterpret_cast<const uint16_t*>(accept_payload.data() + 1));
      if (3 + reason_len <= accept_payload.size()) {
        reason = std::string(reinterpret_cast<const char*>(accept_payload.data() + 3), reason_len);
      }
    }
    throw std::runtime_error("Transfer rejected: " + reason);
  }
  
  std::cout << "[RECV] Accepted! Transfer accepted successfully.\n";
  std::cout << "[RECV] Note: In Phase 3/4, file will be saved on server after sender completes upload.\n";
  std::cout << "[RECV] File download protocol will be implemented in later phases.\n";
  std::cout << "[RECV] SUCCESS! Transfer accepted. File will be available on server.\n";
  std::cout << "[RECV] (Connection will close now - sender will complete upload independently)\n";
}

// Phase 9: consume KEY_EXCHANGE_PUBKEY (type 60) without doing key exchange
static void consume_key_exchange_pubkey(boost::asio::ip::tcp::socket& sock) {
  Header h = read_header(sock);
  if (h.type == 60) { // KEY_EXCHANGE_PUBKEY
    auto payload = read_payload(sock, ntohl(h.len_be));
    std::cout << "[ADMIN] Consumed KEY_EXCHANGE_PUBKEY (" << payload.size() << " bytes)\n";
  } else {
    std::cerr << "[ADMIN] Warning: expected KEY_EXCHANGE_PUBKEY (60), got type " << (int)h.type << "\n";
  }
}

// Phase 9: Set throttle (THROTTLE_SET = 70)
static void do_set_throttle(boost::asio::ip::tcp::socket& sock,
                             uint8_t scope, uint64_t user_id, uint64_t bps) {
  consume_key_exchange_pubkey(sock);

  std::vector<uint8_t> payload;
  payload.push_back(scope);
  if (scope == 1) {
    uint64_t uid_be = htobe64_portable(user_id);
    payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&uid_be),
                   reinterpret_cast<const uint8_t*>(&uid_be) + 8);
  }
  uint64_t bps_be = htobe64_portable(bps);
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&bps_be),
                 reinterpret_cast<const uint8_t*>(&bps_be) + 8);

  auto frame = make_frame(70, payload); // THROTTLE_SET = 70
  boost::asio::write(sock, boost::asio::buffer(frame));

  std::cout << "[ADMIN] THROTTLE_SET sent: scope=" << (scope == 0 ? "global" : "user")
            << " bps=" << bps << (bps == 0 ? " (unlimited)" : "") << "\n";

  // Read response
  auto h = read_header(sock);
  if (h.type == 71) { // THROTTLE_SET_RESP
    auto resp = read_payload(sock, ntohl(h.len_be));
    if (resp.size() >= 1 && resp[0] == 1) {
      std::cout << "[ADMIN] THROTTLE_SET_RESP: OK\n";
    } else {
      std::cerr << "[ADMIN] THROTTLE_SET_RESP: FAIL\n";
    }
  } else {
    std::cerr << "[ADMIN] Unexpected response type " << (int)h.type << "\n";
  }
}

// Phase 9: List active transfers (TRANSFER_LIST_REQ = 72)
static void do_list_transfers(boost::asio::ip::tcp::socket& sock) {
  consume_key_exchange_pubkey(sock);

  auto frame = make_frame(72, {}); // TRANSFER_LIST_REQ = 72
  boost::asio::write(sock, boost::asio::buffer(frame));

  auto h = read_header(sock);
  if (h.type != 73) { // TRANSFER_LIST_RESP
    std::cerr << "[ADMIN] Expected TRANSFER_LIST_RESP (73), got type " << (int)h.type << "\n";
    return;
  }

  auto payload = read_payload(sock, ntohl(h.len_be));
  if (payload.size() < 2) {
    std::cout << "[ADMIN] No transfers.\n";
    return;
  }

  size_t pos = 0;
  uint16_t count = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
  pos += 2;

  std::cout << "[ADMIN] Active transfers: " << count << "\n";

  const char* state_names[] = {"OFFERED", "ACCEPTED", "RECEIVING", "COMPLETED", "FAILED"};

  for (uint16_t i = 0; i < count && pos < payload.size(); i++) {
    if (pos + 8 > payload.size()) break;
    uint64_t tid = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos));
    pos += 8;

    if (pos + 1 > payload.size()) break;
    uint8_t state = payload[pos]; pos += 1;

    // sender
    if (pos + 2 > payload.size()) break;
    uint16_t slen = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos)); pos += 2;
    std::string sender(reinterpret_cast<const char*>(payload.data() + pos), slen); pos += slen;

    // receiver
    if (pos + 2 > payload.size()) break;
    uint16_t rlen = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos)); pos += 2;
    std::string receiver(reinterpret_cast<const char*>(payload.data() + pos), rlen); pos += rlen;

    // filename
    if (pos + 2 > payload.size()) break;
    uint16_t flen = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos)); pos += 2;
    std::string filename(reinterpret_cast<const char*>(payload.data() + pos), flen); pos += flen;

    // file_size, bytes_received, speed
    if (pos + 24 > payload.size()) break;
    uint64_t file_size = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos)); pos += 8;
    uint64_t bytes_rx  = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos)); pos += 8;
    uint64_t speed     = be64toh_portable(*reinterpret_cast<const uint64_t*>(payload.data() + pos)); pos += 8;

    double pct = file_size > 0 ? (100.0 * bytes_rx / file_size) : 0;

    std::cout << "  [" << tid << "] " << sender << " -> " << receiver
              << " | " << filename
              << " | " << (state < 5 ? state_names[state] : "?")
              << " | " << bytes_rx << "/" << file_size
              << " (" << std::fixed << std::setprecision(1) << pct << "%)"
              << " | " << (speed / 1024) << " KB/s\n";
  }
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage:\n";
    std::cerr << "  send: " << argv[0] << " send <username> <password> <receiver_username> <filepath> [host] [port] [--compress] [--corrupt-upload-once <chunk_index>]\n";
    std::cerr << "  recv: " << argv[0] << " recv <username> <password> <transfer_id> <output_path> [host] [port]\n";
    std::cerr << "  resume-send: " << argv[0] << " resume-send <username> <password> <transfer_id> <filepath> [host] [port]\n";
    std::cerr << "  set-throttle: " << argv[0] << " set-throttle <global|user> <bps> [user_id] [host] [port]\n";
    std::cerr << "  list-transfers: " << argv[0] << " list-transfers [host] [port]\n";
    return 1;
  }
  
  std::string cmd = argv[1];

  // Phase 9: admin commands that don't need username/password
  if (cmd == "set-throttle" || cmd == "list-transfers") {
    std::string host = "127.0.0.1";
    uint16_t port = 9000;

    if (cmd == "set-throttle") {
      if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " set-throttle <global|user> <bps> [user_id] [host] [port]\n";
        return 1;
      }
      std::string scope_str = argv[2];
      uint64_t bps = std::stoull(argv[3]);
      uint8_t scope = (scope_str == "user") ? 1 : 0;
      uint64_t user_id = 0;
      int next_arg = 4;
      if (scope == 1 && argc > 4) {
        user_id = std::stoull(argv[4]);
        next_arg = 5;
      }
      if (argc > next_arg) host = argv[next_arg];
      if (argc > next_arg + 1) port = static_cast<uint16_t>(std::stoi(argv[next_arg + 1]));

      try {
        boost::asio::io_context io;
        boost::asio::ip::tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::ip::tcp::socket sock(io);
        boost::asio::connect(sock, endpoints);
        do_set_throttle(sock, scope, user_id, bps);
      } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
      }
    } else { // list-transfers
      if (argc > 2) host = argv[2];
      if (argc > 3) port = static_cast<uint16_t>(std::stoi(argv[3]));

      try {
        boost::asio::io_context io;
        boost::asio::ip::tcp::resolver resolver(io);
        auto endpoints = resolver.resolve(host, std::to_string(port));
        boost::asio::ip::tcp::socket sock(io);
        boost::asio::connect(sock, endpoints);
        do_list_transfers(sock);
      } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
      }
    }
    return 0;
  }

  if (argc < 4) {
    std::cerr << "Error: send/recv/resume-send require at least username and password\n";
    return 1;
  }

  std::string username = argv[2];
  std::string password = argv[3];
  std::string host = "127.0.0.1";
  uint16_t port = 9000;
  int corrupt_chunk_index = -1;
  bool use_compress = false;
  
  std::string receiver_username;
  std::string filepath;
  uint64_t transfer_id = 0;
  std::string output_path;
  
  if (cmd == "send") {
    if (argc < 6) {
      std::cerr << "Error: send requires receiver_username and filepath\n";
      return 1;
    }
    receiver_username = argv[4];
    filepath = argv[5];
    
    // Optional: first non-option = host, second = port, then --compress / --corrupt-upload-once N
    bool host_set = false, port_set = false;
    for (int i = 6; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "--compress") {
        use_compress = true;
      } else if (arg == "--corrupt-upload-once" && i + 1 < argc) {
        corrupt_chunk_index = std::stoi(argv[i + 1]);
        i++;
      } else if (arg[0] != '-') {
        if (!host_set) {
          host = arg;
          host_set = true;
        } else if (!port_set) {
          port = static_cast<uint16_t>(std::stoi(arg));
          port_set = true;
        }
      }
    }
  } else if (cmd == "recv") {
    if (argc < 6) {
      std::cerr << "Error: recv requires transfer_id and output_path\n";
      return 1;
    }
    transfer_id = std::stoull(argv[4]);
    output_path = argv[5];
    if (argc >= 7) host = argv[6];
    if (argc >= 8) port = static_cast<uint16_t>(std::stoi(argv[7]));
  } else if (cmd == "resume-send") {
    if (argc < 6) {
      std::cerr << "Error: resume-send requires transfer_id and filepath\n";
      return 1;
    }
    transfer_id = std::stoull(argv[4]);
    filepath = argv[5];
    if (argc >= 7) host = argv[6];
    if (argc >= 8) port = static_cast<uint16_t>(std::stoi(argv[7]));
  } else {
    std::cerr << "Error: Unknown command '" << cmd << "' (use 'send', 'recv' or 'resume-send')\n";
    return 1;
  }
  
  try {
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    std::cout.flush();
    boost::asio::io_context io;
    boost::asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve(host, std::to_string(port));
    
    boost::asio::ip::tcp::socket sock(io);
    boost::system::error_code connect_ec;
    boost::asio::steady_timer timeout_timer(io, std::chrono::seconds(15));
    timeout_timer.async_wait([&sock](boost::system::error_code ec) {
      if (!ec) sock.cancel();
    });
    boost::asio::async_connect(sock, endpoints, [&connect_ec, &timeout_timer](boost::system::error_code ec, const auto&) {
      timeout_timer.cancel();
      connect_ec = ec;
    });
    io.run();
    timeout_timer.cancel();
    if (connect_ec) {
      std::cerr << "Error: connect: " << connect_ec.message() << " [" << connect_ec.category().name()
                << " at " << connect_ec.value() << "]" << std::endl;
      return 1;
    }
    std::cout << "Connected to " << host << ":" << port << "\n";
    
    // Login first (Phase 8: do_login also performs key exchange and returns session_key for encryption)
    std::vector<uint8_t> session_key;
    if (!do_login(sock, username, password, &session_key)) {
      std::cerr << "Login failed\n";
      return 1;
    }
    
    if (cmd == "send") {
      do_send(sock, receiver_username, filepath, corrupt_chunk_index, use_compress,
              session_key.empty() ? nullptr : &session_key);
    } else if (cmd == "recv") {
      do_recv(sock, transfer_id, output_path);
    } else if (cmd == "resume-send") {
      do_resume_send(sock, transfer_id, filepath);
    }
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

