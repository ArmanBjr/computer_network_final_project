#pragma once
#include <boost/asio.hpp>
#include <deque>
#include <memory>
#include <vector>
#include <iostream>
#include "fsx/protocol/message.h"

namespace fsx::net {

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
  explicit TcpSession(boost::asio::ip::tcp::socket socket);
  void start();

private:
  void log(const std::string& s);

  void do_read_header();
  void do_read_body();

  void send(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload);
  void do_write();

  void handle_message(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload);

  boost::asio::ip::tcp::socket socket_;

  fsx::protocol::MessageHeaderWire header_{};
  std::vector<uint8_t> body_;

  struct OutFrame {
    std::vector<uint8_t> bytes;
  };
  std::deque<OutFrame> outq_;
};

} // namespace fsx::net
