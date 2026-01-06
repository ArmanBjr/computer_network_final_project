// Simple test client for REGISTER/LOGIN
// Compile: g++ -std=c++20 -o test_auth test_auth.cpp -lboost_system -lpthread

#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <arpa/inet.h>

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

static std::vector<uint8_t> make_register_req(const std::string& username, const std::string& email, const std::string& password) {
  std::vector<uint8_t> payload;
  payload.reserve(6 + username.size() + email.size() + password.size());
  
  // Username
  uint16_t u_len = htons(static_cast<uint16_t>(username.size()));
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&u_len), 
                 reinterpret_cast<const uint8_t*>(&u_len) + 2);
  payload.insert(payload.end(), username.begin(), username.end());
  
  // Email
  uint16_t e_len = htons(static_cast<uint16_t>(email.size()));
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&e_len), 
                 reinterpret_cast<const uint8_t*>(&e_len) + 2);
  payload.insert(payload.end(), email.begin(), email.end());
  
  // Password
  uint16_t p_len = htons(static_cast<uint16_t>(password.size()));
  payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(&p_len), 
                 reinterpret_cast<const uint8_t*>(&p_len) + 2);
  payload.insert(payload.end(), password.begin(), password.end());
  
  return make_frame(10, payload); // REGISTER_REQ = 10
}

static std::vector<uint8_t> make_login_req(const std::string& username, const std::string& password) {
  // Same format as register
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

static void read_response(boost::asio::ip::tcp::socket& sock, uint8_t expected_type) {
  Header h{};
  boost::asio::read(sock, boost::asio::buffer(&h, sizeof(h)));
  
  if (ntohl(h.magic_be) != MAGIC) {
    std::cerr << "ERROR: bad magic\n";
    return;
  }
  if (h.version != VERSION) {
    std::cerr << "ERROR: bad version\n";
    return;
  }
  if (h.type != expected_type) {
    std::cerr << "ERROR: unexpected type (got " << (int)h.type << ", expected " << (int)expected_type << ")\n";
    return;
  }
  
  uint32_t len = ntohl(h.len_be);
  if (len > 1024) {
    std::cerr << "ERROR: payload too large\n";
    return;
  }
  
  std::vector<uint8_t> payload(len);
  if (len > 0) {
    boost::asio::read(sock, boost::asio::buffer(payload));
  }
  
  if (expected_type == 11) { // REGISTER_RESP
    if (payload.size() < 3) {
      std::cerr << "ERROR: REGISTER_RESP too short\n";
      return;
    }
    bool ok = payload[0] != 0;
    uint16_t msg_len = ntohs(*reinterpret_cast<uint16_t*>(payload.data() + 1));
    std::string msg(reinterpret_cast<const char*>(payload.data() + 3), msg_len);
    std::cout << "REGISTER_RESP: ok=" << (ok ? "true" : "false") << " msg=" << msg << "\n";
  } else if (expected_type == 13) { // LOGIN_RESP
    if (payload.size() < 3) {
      std::cerr << "ERROR: LOGIN_RESP too short\n";
      return;
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
    
    if (pos + 2 <= payload.size()) {
      uint16_t msg_len = ntohs(*reinterpret_cast<uint16_t*>(payload.data() + pos));
      pos += 2;
      if (pos + msg_len <= payload.size()) {
        std::string msg(reinterpret_cast<const char*>(payload.data() + pos), msg_len);
        std::cout << "LOGIN_RESP: ok=" << (ok ? "true" : "false");
        if (ok) std::cout << " token=" << token.substr(0, 16) << "...";
        std::cout << " msg=" << msg << "\n";
      }
    }
  }
}

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  uint16_t port = 9000;
  std::string cmd = "register";
  std::string username = "testuser";
  std::string email = "testuser@example.com";
  std::string password = "testpass123";

  if (argc >= 2) cmd = argv[1];
  if (argc >= 3) username = argv[2];
  if (argc >= 4) password = argv[3];
  if (argc >= 5) host = argv[4];
  if (argc >= 6) port = static_cast<uint16_t>(std::stoi(argv[5]));
  
  // For register, email can be provided as 4th arg, or default
  if (cmd == "register" && argc >= 5) {
    email = argv[4];
    if (argc >= 6) host = argv[5];
    if (argc >= 7) port = static_cast<uint16_t>(std::stoi(argv[6]));
  }

  try {
    boost::asio::io_context io;
    boost::asio::ip::tcp::resolver resolver(io);
    auto endpoints = resolver.resolve(host, std::to_string(port));

    boost::asio::ip::tcp::socket sock(io);
    boost::asio::connect(sock, endpoints);
    std::cout << "Connected to " << host << ":" << port << "\n";

    if (cmd == "register") {
      std::cout << "Sending REGISTER_REQ: username=" << username << " email=" << email << "\n";
      auto frame = make_register_req(username, email, password);
      boost::asio::write(sock, boost::asio::buffer(frame));
      read_response(sock, 11); // REGISTER_RESP
    } else if (cmd == "login") {
      std::cout << "Sending LOGIN_REQ: username=" << username << "\n";
      auto frame = make_login_req(username, password);
      boost::asio::write(sock, boost::asio::buffer(frame));
      read_response(sock, 13); // LOGIN_RESP
    } else {
      std::cerr << "Unknown command: " << cmd << " (use 'register' or 'login')\n";
      return 1;
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
}

