#pragma once
#include <boost/asio.hpp>

namespace fsx::net {

class TcpServer {
public:
  TcpServer(boost::asio::io_context& io, uint16_t port);
  void start();

private:
  void do_accept();

  boost::asio::io_context& io_;
  boost::asio::ip::tcp::acceptor acceptor_;
};

} // namespace fsx::net
