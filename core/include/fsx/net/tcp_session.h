#pragma once
#include <boost/asio.hpp>
#include <deque>
#include <memory>
#include <vector>
#include <iostream>
#include "fsx/protocol/message.h"
#include "fsx/net/auth_handler.h"

namespace fsx::net {

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
  TcpSession(boost::asio::ip::tcp::socket socket, AuthHandler& auth_handler);
  void start();

  // Auth state
  bool is_authenticated() const { return !token_.empty(); }
  const std::string& token() const { return token_; }
  const std::string& username() const { return username_; }
  long long user_id() const { return user_id_; }

  void set_auth(const std::string& token, long long user_id, const std::string& username) {
    token_ = token;
    user_id_ = user_id;
    username_ = username;
  }

  void clear_auth() {
    token_.clear();
    user_id_ = 0;
    username_.clear();
  }

private:
  void log(const std::string& s);

  void do_read_header();
  void do_read_body();

  void send(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload);
  void do_write();

  void handle_message(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload);

  boost::asio::ip::tcp::socket socket_;
  AuthHandler& auth_handler_;

  // Auth state
  std::string token_;
  long long user_id_ = 0;
  std::string username_;

  fsx::protocol::MessageHeaderWire header_{};
  std::vector<uint8_t> body_;

  struct OutFrame {
    std::vector<uint8_t> bytes;
  };
  std::deque<OutFrame> outq_;
};

} // namespace fsx::net
