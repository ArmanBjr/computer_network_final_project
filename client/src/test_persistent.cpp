// Persistent test client - stays connected after login
// Compile: g++ -std=c++20 -o test_persistent test_persistent.cpp -lboost_system -lpthread

#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <arpa/inet.h>
#include <thread>
#include <chrono>

static constexpr uint32_t MAGIC = 0x46535831; // FSX1
static constexpr uint8_t VERSION = 1;

#pragma pack(push, 1)
struct Header {
  uint32_t magic_be;
  uint8_t  version;
  uint8_t  type;
  uint32_t len_be;
  uint16_t reserved_be;
};
#pragma pack(pop)

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

static bool read_login_response(boost::asio::ip::tcp::socket& sock) {
  Header h{};
  boost::asio::read(sock, boost::asio::buffer(&h, sizeof(h)));
  
  if (ntohl(h.magic_be) != MAGIC) {
    std::cerr << "ERROR: bad magic\n";
    return false;
  }
  if (h.version != VERSION) {
    std::cerr << "ERROR: bad version\n";
    return false;
  }
  if (h.type != 13) { // LOGIN_RESP
    std::cerr << "ERROR: unexpected type (got " << (int)h.type << ", expected 13)\n";
    return false;
  }
  
  uint32_t len = ntohl(h.len_be);
  if (len > 1024) {
    std::cerr << "ERROR: payload too large\n";
    return false;
  }
  
  std::vector<uint8_t> payload(len);
  if (len > 0) {
    boost::asio::read(sock, boost::asio::buffer(payload));
  }
  
  if (payload.size() < 3) {
    std::cerr << "ERROR: LOGIN_RESP too short\n";
    return false;
  }
  
  bool ok = payload[0] != 0;
  size_t pos = 1;
  
  std::string token;
  if (ok && pos + 2 <= payload.size()) {
    uint16_t token_len = ntohs(*reinterpret_cast<uint16_t*>(payload.data() + pos));
    pos += 2;
    if (pos + token_len <= payload.size()) {
      token = std::string(reinterpret_cast<const char*>(payload.data() + pos), token_len);
      pos += token_len;
    }
  }
  
  if (ok) {
    std::cout << "LOGIN_RESP: ok=true token=" << token.substr(0, 16) << "...\n";
    std::cout << "Connection kept alive. Press Ctrl+C to disconnect.\n";
    return true;
  } else {
    std::cout << "LOGIN_RESP: ok=false\n";
    return false;
  }
}

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  uint16_t port = 9000;
  std::string username = "testuser";
  std::string password = "testpass123";

  if (argc >= 2) username = argv[1];
  if (argc >= 3) password = argv[2];
  if (argc >= 4) host = argv[3];
  if (argc >= 5) port = static_cast<uint16_t>(std::stoi(argv[4]));

  try {
    boost::asio::io_context io;
    boost::asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    boost::asio::ip::tcp::socket sock(io);
    boost::asio::connect(sock, endpoints);
    std::cout << "Connected to " << host << ":" << port << "\n";

    std::cout << "Sending LOGIN_REQ: username=" << username << "\n";
    auto frame = make_login_req(username, password);
    boost::asio::write(sock, boost::asio::buffer(frame));
    
    if (!read_login_response(sock)) {
      return 1;
    }

    // Keep connection alive - send PING every 5 seconds
    std::cout << "Staying connected... (sending PING every 5 seconds)\n";
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(5));
      
      // Send PING
      std::vector<uint8_t> ping_payload{'p', 'i', 'n', 'g'};
      auto ping_frame = make_frame(2, ping_payload); // PING = 2
      try {
        boost::asio::write(sock, boost::asio::buffer(ping_frame));
        std::cout << "Sent PING\n";
      } catch (const std::exception& e) {
        std::cerr << "Connection lost: " << e.what() << "\n";
        break;
      }
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

