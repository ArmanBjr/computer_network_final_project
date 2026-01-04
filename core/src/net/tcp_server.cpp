#include "fsx/net/tcp_server.h"
#include "fsx/net/tcp_session.h"
#include "fsx/net/session_manager.h"
#include <iostream>

namespace fsx::net {

TcpServer::TcpServer(boost::asio::io_context& io, uint16_t port, AuthHandler& auth_handler, SessionManager& session_manager)
  : io_(io),
    acceptor_(io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
    auth_handler_(auth_handler),
    session_manager_(session_manager) {}

void TcpServer::start() {
  std::cout << "[core] listening on 0.0.0.0:" << acceptor_.local_endpoint().port() << "\n";
  std::cout.flush();
  do_accept();
}

void TcpServer::do_accept() {
  acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
    if (!ec) {
      auto s = std::make_shared<TcpSession>(std::move(socket), auth_handler_, session_manager_);
      s->start();
    } else {
      std::cout << "[core] accept error: " << ec.message() << "\n";
    }
    do_accept();
  });
}

} // namespace fsx::net
