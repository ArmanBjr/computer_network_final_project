#pragma once
#include <boost/asio.hpp>
#include <deque>
#include <memory>
#include <vector>
#include <iostream>
#include "fsx/protocol/message.h"
#include "fsx/net/auth_handler.h"

namespace fsx::net {
class SessionManager;
}

namespace fsx::transfer {
class TransferManager;
}

namespace fsx::storage {
class FileStore;
class ResumeStore;
}

namespace fsx::db {
class UserRepository;
}

namespace fsx::crypto {
class RsaKeyPair;
}

namespace fsx::voice {
class VoiceManager;
}

namespace fsx::net {

class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
  TcpSession(boost::asio::ip::tcp::socket socket, 
             AuthHandler& auth_handler, 
             SessionManager& session_manager,
             fsx::transfer::TransferManager& transfer_manager,
             fsx::storage::FileStore& file_store,
             fsx::storage::ResumeStore& resume_store,
             fsx::db::UserRepository& user_repository,
             fsx::crypto::RsaKeyPair& rsa_keypair,
             std::shared_ptr<fsx::voice::VoiceManager> voice_manager = nullptr,
             uint16_t udp_voice_port = 9001);
  void start();

  // Auth state
  bool is_authenticated() const { return !token_.empty(); }
  const std::string& token() const { return token_; }
  const std::string& username() const { return username_; }
  long long user_id() const { return user_id_; }

  // Phase 8: session key
  bool has_session_key() const { return session_key_.size() == 32; }
  const std::vector<uint8_t>& session_key() const { return session_key_; }

  // Public send (needed by SessionManager to push notifications)
  void send(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload);

  void set_auth(const std::string& token, long long user_id, const std::string& username) {
    token_ = token;
    user_id_ = user_id;
    username_ = username;
  }

  void clear_auth() {
    token_.clear();
    user_id_ = 0;
    username_.clear();
  }

private:
  void log(const std::string& s);
  std::string get_remote_endpoint() const;
  std::string get_token_short() const;

  void do_read_header();
  void do_read_body();

  void do_write();

  void handle_message(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload);
  
  // File transfer handlers (Phase 3)
  void handle_file_offer_req(const std::vector<uint8_t>& payload);
  void handle_file_accept_req(const std::vector<uint8_t>& payload);
  void handle_file_chunk(const std::vector<uint8_t>& payload);
  void handle_file_done(const std::vector<uint8_t>& payload);
  
  // Phase 4: Upload with CRC32 validation
  void handle_file_upload_chunk(const std::vector<uint8_t>& payload);
  
  // Phase 5: Resume functionality
  void handle_resume_query(const std::vector<uint8_t>& payload);

  // Phase 8: Key exchange
  void handle_key_exchange_session_key(const std::vector<uint8_t>& payload);

  // Phase 9: Throttle & transfer list
  void handle_throttle_set(const std::vector<uint8_t>& payload);
  void handle_transfer_list_req(const std::vector<uint8_t>& payload);

  /// Send ACK, possibly delayed by throttle.
  void send_upload_ack_throttled(uint64_t transfer_id, uint32_t chunk_index,
                                 size_t chunk_bytes);

  // Phase 10: Voice chat signaling
  void handle_voice_call_req(const std::vector<uint8_t>& payload);
  void handle_voice_call_resp(const std::vector<uint8_t>& payload);
  void handle_voice_end(const std::vector<uint8_t>& payload);
  // Phase 11: Voice session list (admin)
  void handle_voice_session_list_req(const std::vector<uint8_t>& payload);

  boost::asio::ip::tcp::socket socket_;
  AuthHandler& auth_handler_;
  SessionManager& session_manager_;
  fsx::transfer::TransferManager& transfer_manager_;
  fsx::storage::FileStore& file_store_;
  fsx::storage::ResumeStore& resume_store_;
  fsx::db::UserRepository& user_repository_;
  fsx::crypto::RsaKeyPair& rsa_keypair_;

  // Auth state
  std::string token_;
  long long user_id_ = 0;
  std::string username_;

  // Phase 8: AES session key (32 bytes) after KEY_EXCHANGE_SESSION_KEY
  std::vector<uint8_t> session_key_;

  // Phase 10: Voice
  std::shared_ptr<fsx::voice::VoiceManager> voice_manager_;
  uint16_t udp_voice_port_ = 9001;
  uint32_t active_voice_session_id_ = 0; // 0 = no active call
  uint32_t pending_voice_session_id_ = 0; // incoming call pending accept
  std::string pending_voice_caller_; // caller username for pending call

  fsx::protocol::MessageHeaderWire header_{};
  std::vector<uint8_t> body_;

  struct OutFrame {
    std::vector<uint8_t> bytes;
  };
  std::deque<OutFrame> outq_;
};

} // namespace fsx::net
