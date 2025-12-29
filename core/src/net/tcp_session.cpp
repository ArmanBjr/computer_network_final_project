#include "fsx/net/tcp_session.h"
#include "fsx/protocol/auth_messages.h"
#include <chrono>
#include <sstream>

namespace fsx::net {

static std::string now_ts() {
  using namespace std::chrono;
  auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  return std::to_string(ms);
}

TcpSession::TcpSession(boost::asio::ip::tcp::socket socket, AuthHandler& auth_handler)
  : socket_(std::move(socket)), auth_handler_(auth_handler) {}

void TcpSession::log(const std::string& s) {
  std::cout << "[sess " << now_ts() << "] " << s << "\n";
  std::cout.flush();
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
        if (session_manager_ && !token_.empty()) {
          session_manager_->remove_session(token_);
        }
        return;
      }
      if (n != sizeof(header_)) {
        log("DISCONNECTED (header size mismatch)");
        return;
      }

      try {
        fsx::protocol::validate_header(header_);
      } catch (const std::exception& e) {
        log(std::string("DISCONNECTED (bad header): ") + e.what());
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
        return;
      }
      if (n != body_.size()) {
        log("DISCONNECTED (body size mismatch)");
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
      log("RECV REGISTER_REQ username=" + req.username);
      auto resp = auth_handler_.handle_register(req);
      auto resp_payload = resp.serialize();
      send(fsx::protocol::MsgType::REGISTER_RESP, resp_payload);
      log("SENT REGISTER_RESP ok=" + std::string(resp.ok ? "true" : "false") + " msg=" + resp.msg);
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
      log("RECV LOGIN_REQ username=" + req.username);
      auto resp = auth_handler_.handle_login(req);
      auto resp_payload = resp.serialize();
      send(fsx::protocol::MsgType::LOGIN_RESP, resp_payload);
      log("SENT LOGIN_RESP ok=" + std::string(resp.ok ? "true" : "false") + 
          (resp.ok ? " token_len=" + std::to_string(resp.token.size()) : "") + 
          " msg=" + resp.msg);
      
      // If login successful, set auth state (session manager will be updated by TcpServer)
      if (resp.ok) {
        // Get user info from auth handler (we need to store it temporarily)
        // For now, we'll get it from the response token and username
        // Note: In a real implementation, we'd get user_id from the handler
        // For Phase 2, we'll use username as identifier
        // TODO: AuthHandler should return user_id in LoginResp
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
    // This will be handled by TcpServer via callback
    log("RECV ONLINE_LIST_REQ");
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
        return;
      }
      outq_.pop_front();
      if (!outq_.empty()) do_write();
    }
  );
}

} // namespace fsx::net
