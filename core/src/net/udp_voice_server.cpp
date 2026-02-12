#include "fsx/net/udp_voice_server.h"
#include "fsx/voice/voice_manager.h"
#include <iostream>
#include <cstring>

#ifdef __linux__
  #include <endian.h>
#elif defined(__APPLE__)
  #include <libkern/OSByteOrder.h>
  #define be32toh(x) OSSwapBigToHostInt32(x)
#elif defined(_WIN32)
  #include <winsock2.h>
  #define be32toh(x) ntohl(x)
#endif

namespace fsx::net {

UdpVoiceServer::UdpVoiceServer(boost::asio::io_context& ioc,
                               uint16_t port,
                               std::shared_ptr<fsx::voice::VoiceManager> vmgr)
  : socket_(ioc, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), port)),
    vmgr_(std::move(vmgr)) {
  std::cout << "[udp-voice] listening on port " << port << "\n";
  std::cout.flush();
}

void UdpVoiceServer::start() {
  do_receive();
}

void UdpVoiceServer::stop() {
  boost::system::error_code ec;
  socket_.cancel(ec);
  socket_.close(ec);
}

void UdpVoiceServer::do_receive() {
  socket_.async_receive_from(
    boost::asio::buffer(recv_buf_), remote_ep_,
    [this](const boost::system::error_code& ec, std::size_t bytes_recvd) {
      handle_receive(ec, bytes_recvd);
    });
}

void UdpVoiceServer::handle_receive(const boost::system::error_code& ec,
                                    std::size_t bytes_recvd) {
  if (ec) {
    if (ec == boost::asio::error::operation_aborted) return;
    std::cerr << "[udp-voice] receive error: " << ec.message() << "\n";
    do_receive();
    return;
  }

  // Minimum frame: 4(session_id) + 4(seq) + 4(ts) + 2(opus_len) = 14 bytes
  if (bytes_recvd < 14) {
    do_receive();
    return;
  }

  // Parse session_id from first 4 bytes (big-endian)
  uint32_t session_id_be;
  std::memcpy(&session_id_be, recv_buf_.data(), 4);
  uint32_t session_id = be32toh(session_id_be);

  // Register endpoint (idempotent after the first call)
  vmgr_->register_endpoint(session_id, remote_ep_);

  // Get relay target
  auto target = vmgr_->get_relay_target(session_id, remote_ep_);
  if (!target) {
    // Other side hasn't sent a UDP frame yet — drop silently
    do_receive();
    return;
  }

  // Forward the entire raw datagram to the other participant
  auto buf = std::make_shared<std::vector<uint8_t>>(
    recv_buf_.data(), recv_buf_.data() + bytes_recvd);

  socket_.async_send_to(
    boost::asio::buffer(*buf), *target,
    [this, buf, session_id](const boost::system::error_code& ec, std::size_t /*sent*/) {
      if (!ec) {
        vmgr_->record_relayed(session_id);
      }
    });

  do_receive();
}

} // namespace fsx::net
