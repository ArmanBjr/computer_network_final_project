// File Transfer Test Client (Phase 3)
// Usage:
//   send: ./test_file_transfer send <username> <password> <receiver_username> <filepath> [host] [port]
//   recv: ./test_file_transfer recv <username> <password> <transfer_id> <output_path> [host] [port]

#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <arpa/inet.h>

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

static bool do_login(boost::asio::ip::tcp::socket& sock, const std::string& username, const std::string& password) {
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
                    const std::string& filepath) {
  // Check file exists
  if (!std::filesystem::exists(filepath)) {
    throw std::runtime_error("File not found: " + filepath);
  }
  
  auto file_size = std::filesystem::file_size(filepath);
  auto filename = std::filesystem::path(filepath).filename().string();
  
  std::cout << "[SEND] File: " << filename << " (" << file_size << " bytes)\n";
  std::cout << "[SEND] Receiver: " << receiver_username << "\n";
  
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
  
  // Read and send chunks
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
    
    std::cout << "[SEND] Sending chunk " << chunk_index << " (" << bytes_read << " bytes)...\n";
    std::cout.flush();
    
    auto chunk_frame = make_file_chunk(transfer_id, chunk_index, chunk_data);
    boost::asio::write(sock, boost::asio::buffer(chunk_frame));
    
    total_sent += bytes_read;
    std::cout << "[SEND] Chunk " << chunk_index << " sent: " << bytes_read << " bytes (total: " << total_sent << "/" << file_size << ")\n";
    std::cout.flush();
    
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
    std::cout << "[SEND] SUCCESS! File saved at: " << path << "\n";
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
  std::cout << "[RECV] Note: In MVP, file will be saved on server after sender completes upload.\n";
  std::cout << "[RECV] File download protocol will be implemented in later phases.\n";
  std::cout << "[RECV] SUCCESS! Transfer accepted. File will be available on server.\n";
}

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage:\n";
    std::cerr << "  send: " << argv[0] << " send <username> <password> <receiver_username> <filepath> [host] [port]\n";
    std::cerr << "  recv: " << argv[0] << " recv <username> <password> [host] [port]\n";
    return 1;
  }
  
  std::string cmd = argv[1];
  std::string username = argv[2];
  std::string password = argv[3];
  std::string host = "127.0.0.1";
  uint16_t port = 9000;
  
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
    if (argc >= 7) host = argv[6];
    if (argc >= 8) port = static_cast<uint16_t>(std::stoi(argv[7]));
  } else if (cmd == "recv") {
    if (argc < 6) {
      std::cerr << "Error: recv requires transfer_id and output_path\n";
      return 1;
    }
    transfer_id = std::stoull(argv[4]);
    output_path = argv[5];
    if (argc >= 7) host = argv[6];
    if (argc >= 8) port = static_cast<uint16_t>(std::stoi(argv[7]));
  } else {
    std::cerr << "Error: Unknown command '" << cmd << "' (use 'send' or 'recv')\n";
    return 1;
  }
  
  try {
    boost::asio::io_context io;
    boost::asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve(host, std::to_string(port));
    
    boost::asio::ip::tcp::socket sock(io);
    boost::asio::connect(sock, endpoints);
    std::cout << "Connected to " << host << ":" << port << "\n";
    
    // Login first
    if (!do_login(sock, username, password)) {
      std::cerr << "Login failed\n";
      return 1;
    }
    
    if (cmd == "send") {
      do_send(sock, receiver_username, filepath);
    } else if (cmd == "recv") {
      do_recv(sock, transfer_id, output_path);
    }
    
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

