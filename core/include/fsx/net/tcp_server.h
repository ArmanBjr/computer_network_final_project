#pragma once
#include <boost/asio.hpp>
#include "fsx/net/auth_handler.h"

namespace fsx::net {

class TcpServer {
public:
  TcpServer(boost::asio::io_context& io, uint16_t port, AuthHandler& auth_handler);
  void start();

private:
  void do_accept();

  boost::asio::io_context& io_;
  boost::asio::ip::tcp::acceptor acceptor_;
  AuthHandler& auth_handler_;
};

} // namespace fsx::net
