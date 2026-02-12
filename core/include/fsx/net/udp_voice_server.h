#pragma once
#include <boost/asio.hpp>
#include <array>
#include <memory>

namespace fsx::voice { class VoiceManager; }

namespace fsx::net {

/**
 * Async UDP server for voice frame relay.
 *
 * Frame format (lightweight, no FSX header):
 *   [4 bytes session_id BE] [4 bytes seq BE]
 *   [4 bytes timestamp_ms BE] [2 bytes opus_len BE] [opus_data ...]
 *
 * On each received datagram the server:
 *   1. Parses session_id from the first 4 bytes.
 *   2. Registers the sender endpoint (if new) with VoiceManager.
 *   3. Looks up the relay target and forwards the raw datagram as-is.
 */
class UdpVoiceServer {
public:
  UdpVoiceServer(boost::asio::io_context& ioc,
                 uint16_t port,
                 std::shared_ptr<fsx::voice::VoiceManager> vmgr);

  /// Start the async receive loop.
  void start();

  /// Stop (cancel socket).
  void stop();

private:
  void do_receive();
  void handle_receive(const boost::system::error_code& ec, std::size_t bytes_recvd);

  boost::asio::ip::udp::socket socket_;
  boost::asio::ip::udp::endpoint remote_ep_;
  std::array<uint8_t, 2048> recv_buf_;

  std::shared_ptr<fsx::voice::VoiceManager> vmgr_;
};

} // namespace fsx::net
