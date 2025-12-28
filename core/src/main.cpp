#include <boost/asio.hpp>
#include <iostream>
#include "fsx/net/tcp_server.h"

int main(int argc, char** argv) {
  uint16_t port = 9000;
  if (argc >= 2) port = (uint16_t)std::stoi(argv[1]);

  boost::asio::io_context io;
  fsx::net::TcpServer server(io, port);
  server.start();

  io.run();
  return 0;
}
