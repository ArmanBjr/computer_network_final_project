#include <boost/asio.hpp>
#include <iostream>
#include "fsx/net/tcp_server.h"
#include "fsx/storage/db_client.h"

static void db_smoke_test() {
  fsx::DbConfig cfg = fsx::load_db_config_from_env();

  fsx::DbClient db;
  db.connect(cfg);

  {
    PGresult* r = db.exec("SELECT 1;");
    fsx::DbClient::expect(r, PGRES_TUPLES_OK, "SELECT 1");
    PQclear(r);
  }

  {
    PGresult* r = db.exec("SELECT COUNT(*) FROM users;");
    fsx::DbClient::expect(r, PGRES_TUPLES_OK, "COUNT users");
    PQclear(r);
  }

  std::cout << "[db] connected + schema OK (users table exists)\n";
}

int main(int argc, char** argv) {
  try {
    db_smoke_test();
  } catch (const std::exception& e) {
    std::cerr << "[db] FATAL: " << e.what() << "\n";
    return 1;
  }
  uint16_t port = 9000;
  if (argc >= 2) port = (uint16_t)std::stoi(argv[1]);

  boost::asio::io_context io;
  fsx::net::TcpServer server(io, port);
  server.start();

  io.run();
  return 0;
}
