#include "fsx/net/tcp_session.h"
#include <chrono>
#include <sstream>

namespace fsx::net {

static std::string now_ts() {
  using namespace std::chrono;
  auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  return std::to_string(ms);
}

TcpSession::TcpSession(boost::asio::ip::tcp::socket socket)
  : socket_(std::move(socket)) {}

void TcpSession::log(const std::string& s) {
  std::cout << "[sess " << now_ts() << "] " << s << "\n";
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
