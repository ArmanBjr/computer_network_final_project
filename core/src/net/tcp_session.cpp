#include "fsx/net/tcp_session.h"
#include "fsx/net/session_manager.h"
#include "fsx/protocol/auth_messages.h"
#include "fsx/protocol/online_messages.h"
#include <chrono>
#include <sstream>

namespace fsx::net {

static std::string now_ts() {
  using namespace std::chrono;
  auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  return std::to_string(ms);
}

TcpSession::TcpSession(boost::asio::ip::tcp::socket socket, AuthHandler& auth_handler, SessionManager& session_manager)
  : socket_(std::move(socket)), auth_handler_(auth_handler), session_manager_(session_manager) {}

void TcpSession::log(const std::string& s) {
  std::cout << "[sess " << now_ts() << "] " << s << "\n";
  std::cout.flush();
}

std::string TcpSession::get_remote_endpoint() const {
  try {
    auto ep = socket_.remote_endpoint();
    return ep.address().to_string() + ":" + std::to_string(ep.port());
  } catch (...) {
    return "unknown";
  }
}

std::string TcpSession::get_token_short() const {
  if (token_.empty()) return "";
  return token_.substr(0, 8) + "...";
}

void TcpSession::start() {
  try {
    auto ep = socket_.remote_endpoint();
    std::ostringstream oss;
    oss << "CONNECTED from " << ep.address().to_string() << ":" << ep.port();
    log(oss.str());
  } catch (...) {
    log("CONNECTED (remote_endpoint unavailable)");
  }
  do_read_header();
}

void TcpSession::do_read_header() {
  auto self = shared_from_this();
  boost::asio::async_read(socket_,
    boost::asio::buffer(&header_, sizeof(header_)),
    [this, self](boost::system::error_code ec, std::size_t n) {
      if (ec) {
        log(std::string("DISCONNECTED (read header): ") + ec.message());
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }
      if (n != sizeof(header_)) {
        log("DISCONNECTED (header size mismatch)");
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }

      try {
        fsx::protocol::validate_header(header_);
      } catch (const std::exception& e) {
        log(std::string("DISCONNECTED (bad header): ") + e.what());
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }

      auto len = fsx::protocol::payload_len(header_);
      if (len > 16 * 1024 * 1024) { 
        log("DISCONNECTED (payload too large)");
        return;
      }

      body_.assign(len, 0);
      do_read_body();
    }
  );
}

void TcpSession::do_read_body() {
  auto self = shared_from_this();
  if (body_.empty()) {
    handle_message(static_cast<fsx::protocol::MsgType>(header_.type), body_);
    do_read_header();
    return;
  }

  boost::asio::async_read(socket_,
    boost::asio::buffer(body_.data(), body_.size()),
    [this, self](boost::system::error_code ec, std::size_t n) {
      if (ec) {
        log(std::string("DISCONNECTED (read body): ") + ec.message());
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }
      if (n != body_.size()) {
        log("DISCONNECTED (body size mismatch)");
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }

      handle_message(static_cast<fsx::protocol::MsgType>(header_.type), body_);
      do_read_header();
    }
  );
}

void TcpSession::handle_message(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload) {
  if (type == fsx::protocol::MsgType::HELLO) {
    std::string name(payload.begin(), payload.end());
    log("RECV HELLO name=" + name);
    return;
  }
  if (type == fsx::protocol::MsgType::PING) {
    log("RECV PING -> SEND PONG");
    const std::string pong = "pong";
    send(fsx::protocol::MsgType::PONG, std::vector<uint8_t>(pong.begin(), pong.end()));
    return;
  }
  if (type == fsx::protocol::MsgType::PONG) {
    log("RECV PONG");
    return;
  }
  
  // Auth messages
  if (type == fsx::protocol::MsgType::REGISTER_REQ) {
    try {
      auto req = fsx::protocol::RegisterReq::deserialize(payload);
      log("RECV REGISTER_REQ username=" + req.username + " from=" + get_remote_endpoint());
      auto resp = auth_handler_.handle_register(req);
      auto resp_payload = resp.serialize();
      send(fsx::protocol::MsgType::REGISTER_RESP, resp_payload);
      if (resp.ok) {
        log("AUTH_REGISTER_OK username=" + req.username + " from=" + get_remote_endpoint());
      } else {
        log("AUTH_REGISTER_FAIL username=" + req.username + " reason=" + resp.msg + " from=" + get_remote_endpoint());
      }
    } catch (const std::exception& e) {
      log("REGISTER_REQ error: " + std::string(e.what()));
      fsx::protocol::RegisterResp err_resp;
      err_resp.ok = false;
      err_resp.msg = std::string("error: ") + e.what();
      send(fsx::protocol::MsgType::REGISTER_RESP, err_resp.serialize());
    }
    return;
  }
  
  if (type == fsx::protocol::MsgType::LOGIN_REQ) {
    try {
      auto req = fsx::protocol::LoginReq::deserialize(payload);
      log("RECV LOGIN_REQ username=" + req.username + " from=" + get_remote_endpoint());
      auto resp = auth_handler_.handle_login(req);
      auto resp_payload = resp.serialize();
      send(fsx::protocol::MsgType::LOGIN_RESP, resp_payload);
      
      // If login successful, set auth state and register in session manager
      if (resp.ok) {
        // Set authentication state in this session
        set_auth(resp.token, resp.user_id, resp.username);
        
        // Register session in SessionManager (for online list)
        auto self = shared_from_this();
        session_manager_.add_session(resp.token, self);
        
        log("AUTH_LOGIN_OK username=" + resp.username + " user_id=" + std::to_string(resp.user_id) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint());
        log("ONLINE_ADD username=" + resp.username + " user_id=" + std::to_string(resp.user_id) + 
            " count=" + std::to_string(session_manager_.count()));
      } else {
        log("AUTH_LOGIN_FAIL username=" + req.username + " reason=" + resp.msg + " from=" + get_remote_endpoint());
      }
    } catch (const std::exception& e) {
      log("LOGIN_REQ error: " + std::string(e.what()));
      fsx::protocol::LoginResp err_resp;
      err_resp.ok = false;
      err_resp.msg = std::string("error: ") + e.what();
      send(fsx::protocol::MsgType::LOGIN_RESP, err_resp.serialize());
    }
    return;
  }
  
  if (type == fsx::protocol::MsgType::ONLINE_LIST_REQ) {
    log("ONLINE_LIST_REQ from=" + get_remote_endpoint() + 
        (is_authenticated() ? " user=" + username_ : " unauthenticated"));
    
    // Get online usernames from SessionManager
    auto usernames = session_manager_.get_online_usernames();
    
    // Build response
    fsx::protocol::OnlineListResp resp;
    resp.usernames = usernames;
    
    // Serialize and send
    auto resp_payload = resp.serialize();
    send(fsx::protocol::MsgType::ONLINE_LIST_RESP, resp_payload);
    
    log("ONLINE_LIST_RESP count=" + std::to_string(usernames.size()) + 
        " to=" + get_remote_endpoint());
    return;
  }
  
  log("RECV UNKNOWN type=" + std::to_string(static_cast<int>(type)));
}

void TcpSession::send(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload) {
  // frame = header(12) + payload
  fsx::protocol::MessageHeaderWire h = fsx::protocol::make_header(type, (uint32_t)payload.size());

  OutFrame f;
  f.bytes.resize(sizeof(h) + payload.size());
  std::memcpy(f.bytes.data(), &h, sizeof(h));
  if (!payload.empty()) std::memcpy(f.bytes.data() + sizeof(h), payload.data(), payload.size());

  bool writing = !outq_.empty();
  outq_.push_back(std::move(f));
  if (!writing) do_write();
}

void TcpSession::do_write() {
  auto self = shared_from_this();
  boost::asio::async_write(socket_,
    boost::asio::buffer(outq_.front().bytes),
    [this, self](boost::system::error_code ec, std::size_t) {
      if (ec) {
        log(std::string("DISCONNECTED (write): ") + ec.message());
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }
      outq_.pop_front();
      if (!outq_.empty()) do_write();
    }
  );
}

} // namespace fsx::net
