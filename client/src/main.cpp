#include <boost/asio.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <arpa/inet.h>

static constexpr uint32_t MAGIC = 0x46535831; // FSX1
static constexpr uint8_t VERSION = 1;

#pragma pack(push, 1)
struct Header {
  uint32_t magic_be;
  uint8_t  version;
  uint8_t  type;
  uint32_t len_be;
  uint16_t reserved_be;
};
#pragma pack(pop)

static std::vector<uint8_t> frame(uint8_t type, const std::vector<uint8_t>& payload) {
  Header h{};
  h.magic_be = htonl(MAGIC);
  h.version = VERSION;
  h.type = type;
  h.len_be = htonl((uint32_t)payload.size());
  h.reserved_be = htons(0);

  std::vector<uint8_t> out(sizeof(h) + payload.size());
  std::memcpy(out.data(), &h, sizeof(h));
  if (!payload.empty()) std::memcpy(out.data() + sizeof(h), payload.data(), payload.size());
  return out;
}

int main(int argc, char** argv) {
  std::string host = "127.0.0.1";
  uint16_t port = 9000;
  std::string name = "client";

  if (argc >= 2) name = argv[1];
  if (argc >= 3) host = argv[2];
  if (argc >= 4) port = (uint16_t)std::stoi(argv[3]);

  boost::asio::io_context io;
  boost::asio::ip::tcp::resolver resolver(io);
  auto endpoints = resolver.resolve(host, std::to_string(port));

  boost::asio::ip::tcp::socket sock(io);
  boost::asio::connect(sock, endpoints);

  std::cout << "[" << name << "] connected\n";

  // HELLO
  auto hello_payload = std::vector<uint8_t>(name.begin(), name.end());
  auto hello_frame = frame(1, hello_payload);
  boost::asio::write(sock, boost::asio::buffer(hello_frame));

  // PING
  auto ping_payload = std::vector<uint8_t>{'p','i','n','g'};
  auto ping_frame = frame(2, ping_payload);
  boost::asio::write(sock, boost::asio::buffer(ping_frame));

  // (اختیاری) چند ثانیه باز بمونه تا تو wireshark راحت ببینی
  std::this_thread::sleep_for(std::chrono::seconds(10));
  return 0;
}
