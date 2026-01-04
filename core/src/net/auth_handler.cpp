#include "fsx/net/auth_handler.h"
#include <stdexcept>

namespace fsx::net {

fsx::protocol::RegisterResp AuthHandler::handle_register(const fsx::protocol::RegisterReq& req) {
  fsx::protocol::RegisterResp resp;
  
  // Validate input
  if (req.username.empty() || req.username.size() > 64) {
    resp.ok = false;
    resp.msg = "username must be 1-64 characters";
    return resp;
  }
  if (req.password.empty() || req.password.size() > 128) {
    resp.ok = false;
    resp.msg = "password must be 1-128 characters";
    return resp;
  }

  // Check if user already exists
  auto existing = users_.get_user_by_username(req.username);
  if (existing) {
    resp.ok = false;
    resp.msg = "username already exists";
    return resp;
  }

  // Validate email
  if (req.email.empty() || req.email.size() > 255) {
    resp.ok = false;
    resp.msg = "email must be 1-255 characters";
    return resp;
  }

  // Hash password and create user
  try {
    std::string pass_hash = fsx::auth::hash_password_pbkdf2(req.password);
    long long user_id = users_.create_user(req.username, req.email, pass_hash);
    
    resp.ok = true;
    resp.msg = "user created successfully";
    return resp;
  } catch (const std::exception& e) {
    resp.ok = false;
    resp.msg = std::string("registration failed: ") + e.what();
    return resp;
  }
}

fsx::protocol::LoginResp AuthHandler::handle_login(const fsx::protocol::LoginReq& req) {
  fsx::protocol::LoginResp resp;
  
  // Validate input
  if (req.username.empty() || req.password.empty()) {
    resp.ok = false;
    resp.msg = "username and password required";
    return resp;
  }

  // Get user
  auto user = users_.get_user_by_username(req.username);
  if (!user) {
    resp.ok = false;
    resp.msg = "invalid username or password";
    return resp;
  }

  // Verify password
  if (!fsx::auth::verify_password_pbkdf2(req.password, user->pass_hash)) {
    resp.ok = false;
    resp.msg = "invalid username or password";
    return resp;
  }

  // Create session (24 hours TTL)
  try {
    std::string token = sessions_.create_session(user->id, 24 * 3600);
    resp.ok = true;
    resp.token = token;
    resp.user_id = user->id;
    resp.username = user->username;
    resp.msg = "login successful";
    return resp;
  } catch (const std::exception& e) {
    resp.ok = false;
    resp.msg = std::string("login failed: ") + e.what();
    return resp;
  }
}

} // namespace fsx::net
