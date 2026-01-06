#pragma once
#include <boost/asio.hpp>
#include "fsx/net/auth_handler.h"

namespace fsx::net {
class SessionManager;
}

namespace fsx::transfer {
class TransferManager;
}

namespace fsx::storage {
class FileStore;
}

namespace fsx::db {
class UserRepository;
}

namespace fsx::net {

class TcpServer {
public:
  TcpServer(boost::asio::io_context& io, 
            uint16_t port, 
            AuthHandler& auth_handler, 
            SessionManager& session_manager,
            fsx::transfer::TransferManager& transfer_manager,
            fsx::storage::FileStore& file_store,
            fsx::db::UserRepository& user_repository);
  void start();

  SessionManager& session_manager() { return session_manager_; }

private:
  void do_accept();

  boost::asio::io_context& io_;
  boost::asio::ip::tcp::acceptor acceptor_;
  AuthHandler& auth_handler_;
  SessionManager& session_manager_;
  fsx::transfer::TransferManager& transfer_manager_;
  fsx::storage::FileStore& file_store_;
  fsx::db::UserRepository& user_repository_;
};

} // namespace fsx::net
