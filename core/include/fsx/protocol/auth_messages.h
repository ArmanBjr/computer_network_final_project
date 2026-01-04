#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <stdexcept>
#include <arpa/inet.h>

namespace fsx::protocol {

// REGISTER_REQ payload format:
// u16 username_len (network order)
// bytes username
// u16 email_len (network order)
// bytes email
// u16 password_len (network order)
// bytes password

struct RegisterReq {
  std::string username;
  std::string email;
  std::string password;

  static RegisterReq deserialize(const std::vector<uint8_t>& payload) {
    if (payload.size() < 6) throw std::runtime_error("REGISTER_REQ: payload too short");
    
    size_t pos = 0;
    
    // Username
    uint16_t username_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;
    if (pos + username_len > payload.size()) throw std::runtime_error("REGISTER_REQ: invalid username_len");
    std::string username(reinterpret_cast<const char*>(payload.data() + pos), username_len);
    pos += username_len;
    
    // Email
    if (pos + 2 > payload.size()) throw std::runtime_error("REGISTER_REQ: missing email_len");
    uint16_t email_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;
    if (pos + email_len > payload.size()) throw std::runtime_error("REGISTER_REQ: invalid email_len");
    std::string email(reinterpret_cast<const char*>(payload.data() + pos), email_len);
    pos += email_len;
    
    // Password
    if (pos + 2 > payload.size()) throw std::runtime_error("REGISTER_REQ: missing password_len");
    uint16_t password_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;
    if (pos + password_len > payload.size()) throw std::runtime_error("REGISTER_REQ: invalid password_len");
    std::string password(reinterpret_cast<const char*>(payload.data() + pos), password_len);
    
    RegisterReq req;
    req.username = std::move(username);
    req.email = std::move(email);
    req.password = std::move(password);
    return req;
  }
};

// REGISTER_RESP payload format:
// u8 ok (0=fail, 1=success)
// u16 msg_len (network order)
// bytes msg

struct RegisterResp {
  bool ok;
  std::string msg;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> out;
    out.reserve(3 + msg.size());
    
    out.push_back(ok ? 1 : 0);
    
    uint16_t msg_len_be = htons(static_cast<uint16_t>(msg.size()));
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&msg_len_be), 
               reinterpret_cast<const uint8_t*>(&msg_len_be) + 2);
    
    out.insert(out.end(), msg.begin(), msg.end());
    return out;
  }
};

// LOGIN_REQ payload format: same as REGISTER_REQ
// u16 username_len
// bytes username
// u16 password_len
// bytes password

struct LoginReq {
  std::string username;
  std::string password;

  static LoginReq deserialize(const std::vector<uint8_t>& payload) {
    // Same format as RegisterReq, but return LoginReq
    if (payload.size() < 4) throw std::runtime_error("LOGIN_REQ: payload too short");
    
    size_t pos = 0;
    uint16_t username_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;
    
    if (pos + username_len > payload.size()) throw std::runtime_error("LOGIN_REQ: invalid username_len");
    std::string username(reinterpret_cast<const char*>(payload.data() + pos), username_len);
    pos += username_len;
    
    if (pos + 2 > payload.size()) throw std::runtime_error("LOGIN_REQ: missing password_len");
    uint16_t password_len = ntohs(*reinterpret_cast<const uint16_t*>(payload.data() + pos));
    pos += 2;
    
    if (pos + password_len > payload.size()) throw std::runtime_error("LOGIN_REQ: invalid password_len");
    std::string password(reinterpret_cast<const char*>(payload.data() + pos), password_len);
    
    LoginReq req;
    req.username = std::move(username);
    req.password = std::move(password);
    return req;
  }
};

// LOGIN_RESP payload format:
// u8 ok
// if ok=1:
//   u16 token_len (network order)
//   bytes token
// u16 msg_len (network order)
// bytes msg

struct LoginResp {
  bool ok;
  std::string token;  // only if ok=true
  long long user_id = 0;  // only if ok=true
  std::string username;  // only if ok=true
  std::string msg;

  std::vector<uint8_t> serialize() const {
    std::vector<uint8_t> out;
    size_t est_size = 1 + 2 + (ok ? (2 + token.size()) : 0) + 2 + msg.size();
    out.reserve(est_size);
    
    out.push_back(ok ? 1 : 0);
    
    if (ok) {
      uint16_t token_len_be = htons(static_cast<uint16_t>(token.size()));
      out.insert(out.end(), reinterpret_cast<const uint8_t*>(&token_len_be), 
                 reinterpret_cast<const uint8_t*>(&token_len_be) + 2);
      out.insert(out.end(), token.begin(), token.end());
      
      // user_id (int64_t, network byte order)
      uint64_t user_id_be = htobe64(static_cast<uint64_t>(user_id));
      out.insert(out.end(), reinterpret_cast<const uint8_t*>(&user_id_be),
                 reinterpret_cast<const uint8_t*>(&user_id_be) + 8);
      
      // username
      uint16_t username_len_be = htons(static_cast<uint16_t>(username.size()));
      out.insert(out.end(), reinterpret_cast<const uint8_t*>(&username_len_be),
                 reinterpret_cast<const uint8_t*>(&username_len_be) + 2);
      out.insert(out.end(), username.begin(), username.end());
    }
    
    uint16_t msg_len_be = htons(static_cast<uint16_t>(msg.size()));
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(&msg_len_be), 
               reinterpret_cast<const uint8_t*>(&msg_len_be) + 2);
    out.insert(out.end(), msg.begin(), msg.end());
    
    return out;
  }
};

} // namespace fsx::protocol

