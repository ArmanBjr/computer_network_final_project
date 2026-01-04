#pragma once
#include <boost/asio.hpp>
#include "fsx/net/auth_handler.h"

namespace fsx::net {

class SessionManager;
class TcpSession;

class TcpServer {
public:
  TcpServer(boost::asio::io_context& io, uint16_t port, AuthHandler& auth_handler, SessionManager& session_manager);
  void start();

  SessionManager& session_manager() { return session_manager_; }

private:
  void do_accept();

  boost::asio::io_context& io_;
  boost::asio::ip::tcp::acceptor acceptor_;
  AuthHandler& auth_handler_;
  SessionManager& session_manager_;
};

} // namespace fsx::net
