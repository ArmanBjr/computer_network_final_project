#include "fsx/net/tcp_session.h"
#include "fsx/net/session_manager.h"
#include "fsx/protocol/auth_messages.h"
#include "fsx/protocol/online_messages.h"
#include "fsx/protocol/file_messages.h"
#include "fsx/transfer/transfer_manager.h"
#include "fsx/transfer/integrity.h"
#include "fsx/transfer/zlib_codec.h"
#include "fsx/storage/file_store.h"
#include "fsx/storage/resume_store.h"
#include "fsx/db/user_repository.h"
#include "fsx/crypto/rsa.h"
#include "fsx/crypto/aes_gcm.h"
#include "fsx/voice/voice_manager.h"
#include <chrono>
#include <sstream>
#include <cstdio>
#include <endian.h>  // be64toh / htobe64 (Linux)

namespace fsx::net {

static std::string now_ts() {
  using namespace std::chrono;
  auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
  return std::to_string(ms);
}

TcpSession::TcpSession(boost::asio::ip::tcp::socket socket, 
                       AuthHandler& auth_handler, 
                       SessionManager& session_manager,
                       transfer::TransferManager& transfer_manager,
                       storage::FileStore& file_store,
                       storage::ResumeStore& resume_store,
                       db::UserRepository& user_repository,
                       fsx::crypto::RsaKeyPair& rsa_keypair,
                       std::shared_ptr<fsx::voice::VoiceManager> voice_manager,
                       uint16_t udp_voice_port)
  : socket_(std::move(socket)), 
    auth_handler_(auth_handler), 
    session_manager_(session_manager),
    transfer_manager_(transfer_manager),
    file_store_(file_store),
    resume_store_(resume_store),
    user_repository_(user_repository),
    rsa_keypair_(rsa_keypair),
    voice_manager_(std::move(voice_manager)),
    udp_voice_port_(udp_voice_port) {}

void TcpSession::log(const std::string& s) {
  std::cout << "[sess " << now_ts() << "] " << s << "\n";
  std::cout.flush();
}

std::string TcpSession::get_remote_endpoint() const {
  try {
    auto ep = socket_.remote_endpoint();
    return ep.address().to_string() + ":" + std::to_string(ep.port());
  } catch (...) {
    return "unknown";
  }
}

std::string TcpSession::get_token_short() const {
  if (token_.empty()) return "";
  return token_.substr(0, 8) + "...";
}

void TcpSession::start() {
  try {
    auto ep = socket_.remote_endpoint();
    std::ostringstream oss;
    oss << "CONNECTED from " << ep.address().to_string() << ":" << ep.port();
    log(oss.str());
  } catch (...) {
    log("CONNECTED (remote_endpoint unavailable)");
  }
  // Phase 8: send server public key so client can encrypt session key
  if (rsa_keypair_.is_initialized()) {
    auto pub_der = rsa_keypair_.get_public_der();
    if (!pub_der.empty()) {
      send(fsx::protocol::MsgType::KEY_EXCHANGE_PUBKEY, pub_der);
      log("KEY_EXCHANGE_PUBKEY sent (DER " + std::to_string(pub_der.size()) + " bytes)");
    }
  }
  do_read_header();
}

void TcpSession::do_read_header() {
  auto self = shared_from_this();
  boost::asio::async_read(socket_,
    boost::asio::buffer(&header_, sizeof(header_)),
    [this, self](boost::system::error_code ec, std::size_t n) {
      if (ec) {
        log(std::string("DISCONNECTED (read header): ") + ec.message());
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }
      if (n != sizeof(header_)) {
        log("DISCONNECTED (header size mismatch)");
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }

      try {
        fsx::protocol::validate_header(header_);
      } catch (const std::exception& e) {
        log(std::string("DISCONNECTED (bad header): ") + e.what());
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }

      auto len = fsx::protocol::payload_len(header_);
      if (len > 16 * 1024 * 1024) { 
        log("DISCONNECTED (payload too large)");
        return;
      }

      body_.assign(len, 0);
      do_read_body();
    }
  );
}

void TcpSession::do_read_body() {
  auto self = shared_from_this();
  if (body_.empty()) {
    handle_message(static_cast<fsx::protocol::MsgType>(header_.type), body_);
    do_read_header();
    return;
  }

  boost::asio::async_read(socket_,
    boost::asio::buffer(body_.data(), body_.size()),
    [this, self](boost::system::error_code ec, std::size_t n) {
      if (ec) {
        log(std::string("DISCONNECTED (read body): ") + ec.message());
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }
      if (n != body_.size()) {
        log("DISCONNECTED (body size mismatch)");
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }

      handle_message(static_cast<fsx::protocol::MsgType>(header_.type), body_);
      do_read_header();
    }
  );
}

void TcpSession::handle_message(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload) {
  if (type == fsx::protocol::MsgType::HELLO) {
    std::string name(payload.begin(), payload.end());
    log("RECV HELLO name=" + name);
    return;
  }
  if (type == fsx::protocol::MsgType::PING) {
    log("RECV PING -> SEND PONG");
    const std::string pong = "pong";
    send(fsx::protocol::MsgType::PONG, std::vector<uint8_t>(pong.begin(), pong.end()));
    return;
  }
  if (type == fsx::protocol::MsgType::PONG) {
    log("RECV PONG");
    return;
  }

  // Phase 8: Key exchange — client sends RSA-encrypted 32-byte session key
  if (type == fsx::protocol::MsgType::KEY_EXCHANGE_SESSION_KEY) {
    handle_key_exchange_session_key(payload);
    return;
  }
  
  // Auth messages
  if (type == fsx::protocol::MsgType::REGISTER_REQ) {
    try {
      auto req = fsx::protocol::RegisterReq::deserialize(payload);
      log("RECV REGISTER_REQ username=" + req.username + " from=" + get_remote_endpoint());
      auto resp = auth_handler_.handle_register(req);
      auto resp_payload = resp.serialize();
      send(fsx::protocol::MsgType::REGISTER_RESP, resp_payload);
      if (resp.ok) {
        log("AUTH_REGISTER_OK username=" + req.username + " from=" + get_remote_endpoint());
      } else {
        log("AUTH_REGISTER_FAIL username=" + req.username + " reason=" + resp.msg + " from=" + get_remote_endpoint());
      }
    } catch (const std::exception& e) {
      log("REGISTER_REQ error: " + std::string(e.what()));
      fsx::protocol::RegisterResp err_resp;
      err_resp.ok = false;
      err_resp.msg = std::string("error: ") + e.what();
      send(fsx::protocol::MsgType::REGISTER_RESP, err_resp.serialize());
    }
    return;
  }
  
  if (type == fsx::protocol::MsgType::LOGIN_REQ) {
    try {
      auto req = fsx::protocol::LoginReq::deserialize(payload);
      log("RECV LOGIN_REQ username=" + req.username + " from=" + get_remote_endpoint());
      auto resp = auth_handler_.handle_login(req);
      auto resp_payload = resp.serialize();
      send(fsx::protocol::MsgType::LOGIN_RESP, resp_payload);
      
      // If login successful, set auth state and register in session manager
      if (resp.ok) {
        // Set authentication state in this session
        set_auth(resp.token, resp.user_id, resp.username);
        
        // Register session in SessionManager (for online list)
        auto self = shared_from_this();
        session_manager_.add_session(resp.token, self);
        
        log("AUTH_LOGIN_OK username=" + resp.username + " user_id=" + std::to_string(resp.user_id) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint());
        log("ONLINE_ADD username=" + resp.username + " user_id=" + std::to_string(resp.user_id) + 
            " count=" + std::to_string(session_manager_.count()));
      } else {
        log("AUTH_LOGIN_FAIL username=" + req.username + " reason=" + resp.msg + " from=" + get_remote_endpoint());
      }
    } catch (const std::exception& e) {
      log("LOGIN_REQ error: " + std::string(e.what()));
      fsx::protocol::LoginResp err_resp;
      err_resp.ok = false;
      err_resp.msg = std::string("error: ") + e.what();
      send(fsx::protocol::MsgType::LOGIN_RESP, err_resp.serialize());
    }
    return;
  }
  
  if (type == fsx::protocol::MsgType::ONLINE_LIST_REQ) {
    log("ONLINE_LIST_REQ from=" + get_remote_endpoint() + 
        (is_authenticated() ? " user=" + username_ : " unauthenticated"));
    
    // Get online usernames from SessionManager
    auto usernames = session_manager_.get_online_usernames();
    
    // Build response
    fsx::protocol::OnlineListResp resp;
    resp.usernames = usernames;
    
    // Serialize and send
    auto resp_payload = resp.serialize();
    send(fsx::protocol::MsgType::ONLINE_LIST_RESP, resp_payload);
    
    log("ONLINE_LIST_RESP count=" + std::to_string(usernames.size()) + 
        " to=" + get_remote_endpoint());
    return;
  }
  
  // File transfer handlers (Phase 3)
  if (type == fsx::protocol::MsgType::FILE_OFFER_REQ) {
    log("RECV FILE_OFFER_REQ (type=30) from=" + get_remote_endpoint());
    handle_file_offer_req(payload);
    return;
  }
  
  if (type == fsx::protocol::MsgType::FILE_ACCEPT_REQ) {
    handle_file_accept_req(payload);
    return;
  }
  
  if (type == fsx::protocol::MsgType::FILE_CHUNK) {
    handle_file_chunk(payload);
    return;
  }
  
  // Phase 8: FILE_UPLOAD_CHUNK_ENCRYPTED — decrypt with session key then handle as FILE_UPLOAD_CHUNK
  if (type == fsx::protocol::MsgType::FILE_UPLOAD_CHUNK_ENCRYPTED) {
    if (!has_session_key()) {
      log("FILE_UPLOAD_CHUNK_ENCRYPTED rejected: no session key");
      return;
    }
    auto decrypted = fsx::crypto::AesGcm::decrypt(payload.data(), payload.size(), session_key_.data());
    if (decrypted.empty()) {
      log("FILE_UPLOAD_CHUNK_ENCRYPTED decrypt/auth failed");
      return;
    }
    handle_file_upload_chunk(decrypted);
    return;
  }

  // Phase 4: FILE_UPLOAD_CHUNK with CRC32 validation
  if (type == fsx::protocol::MsgType::FILE_UPLOAD_CHUNK) {
    handle_file_upload_chunk(payload);
    return;
  }
  
  if (type == fsx::protocol::MsgType::FILE_DONE) {
    handle_file_done(payload);
    return;
  }
  
  // Phase 5: Resume query
  if (type == fsx::protocol::MsgType::RESUME_QUERY) {
    handle_resume_query(payload);
    return;
  }
  
  // Phase 9: Throttle & transfer list (admin)
  if (type == fsx::protocol::MsgType::THROTTLE_SET) {
    handle_throttle_set(payload);
    return;
  }
  if (type == fsx::protocol::MsgType::TRANSFER_LIST_REQ) {
    handle_transfer_list_req(payload);
    return;
  }

  // Phase 10: Voice chat signaling
  if (type == fsx::protocol::MsgType::VOICE_CALL_REQ) {
    handle_voice_call_req(payload);
    return;
  }
  if (type == fsx::protocol::MsgType::VOICE_CALL_RESP) {
    handle_voice_call_resp(payload);
    return;
  }
  if (type == fsx::protocol::MsgType::VOICE_END) {
    handle_voice_end(payload);
    return;
  }
  if (type == fsx::protocol::MsgType::VOICE_SESSION_LIST_REQ) {
    handle_voice_session_list_req(payload);
    return;
  }
  
  log("RECV UNKNOWN type=" + std::to_string(static_cast<int>(type)));
}

void TcpSession::send(fsx::protocol::MsgType type, const std::vector<uint8_t>& payload) {
  // frame = header(12) + payload
  fsx::protocol::MessageHeaderWire h = fsx::protocol::make_header(type, (uint32_t)payload.size());

  OutFrame f;
  f.bytes.resize(sizeof(h) + payload.size());
  std::memcpy(f.bytes.data(), &h, sizeof(h));
  if (!payload.empty()) std::memcpy(f.bytes.data() + sizeof(h), payload.data(), payload.size());

  bool writing = !outq_.empty();
  outq_.push_back(std::move(f));
  if (!writing) do_write();
}

void TcpSession::do_write() {
  auto self = shared_from_this();
  boost::asio::async_write(socket_,
    boost::asio::buffer(outq_.front().bytes),
    [this, self](boost::system::error_code ec, std::size_t) {
      if (ec) {
        log(std::string("DISCONNECTED (write): ") + ec.message());
        if (!token_.empty()) {
        size_t count_before = session_manager_.count();
        log("ONLINE_REMOVE username=" + username_ + " user_id=" + std::to_string(user_id_) + 
            " token=" + get_token_short() + " from=" + get_remote_endpoint() + 
            " count_before=" + std::to_string(count_before));
        session_manager_.remove_session(token_);
        clear_auth();
        }
        return;
      }
      outq_.pop_front();
      if (!outq_.empty()) do_write();
    }
  );
}

// File transfer handlers (Phase 3)
void TcpSession::handle_file_offer_req(const std::vector<uint8_t>& payload) {
  if (!is_authenticated()) {
    log("FILE_OFFER_REQ rejected: not authenticated from=" + get_remote_endpoint());
    fsx::protocol::FileOfferResp resp;
    resp.ok = false;
    resp.transfer_id = 0;
    resp.reason = "Not authenticated";
    send(fsx::protocol::MsgType::FILE_OFFER_RESP, resp.serialize());
    return;
  }
  
  try {
    fsx::protocol::FileOfferReq req = fsx::protocol::FileOfferReq::deserialize(payload);
    
    log("FILE_OFFER_REQ from=" + get_remote_endpoint() + 
        " sender=" + username_ + 
        " receiver=" + req.receiver_username + 
        " filename=" + req.filename + 
        " size=" + std::to_string(req.file_size) + 
        " chunk_size=" + std::to_string(req.chunk_size));
    
    // Find receiver user
    auto receiver_user = user_repository_.get_user_by_username(req.receiver_username);
    if (!receiver_user) {
      log("FILE_OFFER_REQ FAIL: receiver not found username=" + req.receiver_username);
      fsx::protocol::FileOfferResp resp;
      resp.ok = false;
      resp.transfer_id = 0;
      resp.reason = "Receiver not found";
      send(fsx::protocol::MsgType::FILE_OFFER_RESP, resp.serialize());
      return;
    }
    
    // Validate chunk size (min 1KB, max 1MB)
    uint32_t chunk_size = req.chunk_size;
    if (chunk_size < 1024) chunk_size = 64 * 1024;  // Default 64KB
    if (chunk_size > 1024 * 1024) chunk_size = 256 * 1024;  // Max 256KB
    
    // Create transfer
    log("FILE_OFFER: creating transfer sender_token=" + (token_.empty() ? "EMPTY" : token_.substr(0, 8) + "..."));
    uint64_t transfer_id = transfer_manager_.create_transfer(
      user_id_,
      username_,
      token_,  // sender_token for notifying sender when receiver accepts
      receiver_user->id,
      receiver_user->username,
      req.filename,
      req.file_size,
      chunk_size
    );
    
    if (transfer_id == 0) {
      log("FILE_OFFER_REQ FAIL: failed to create transfer");
      fsx::protocol::FileOfferResp resp;
      resp.ok = false;
      resp.transfer_id = 0;
      resp.reason = "Failed to create transfer";
      send(fsx::protocol::MsgType::FILE_OFFER_RESP, resp.serialize());
      return;
    }
    
    // Get transfer session to set file paths
    auto session = transfer_manager_.get_transfer(transfer_id);
    if (session) {
      session->temp_file_path = file_store_.get_temp_path(transfer_id, req.filename);
      session->final_file_path = file_store_.get_file_path(transfer_id, req.filename);
    }
    
    log("FILE_OFFER_OK transfer_id=" + std::to_string(transfer_id) + 
        " sender=" + username_ + 
        " receiver=" + req.receiver_username);
    
    fsx::protocol::FileOfferResp resp;
    resp.ok = true;
    resp.transfer_id = transfer_id;
    send(fsx::protocol::MsgType::FILE_OFFER_RESP, resp.serialize());
    
  } catch (const std::exception& e) {
    log("FILE_OFFER_REQ error: " + std::string(e.what()));
    fsx::protocol::FileOfferResp resp;
    resp.ok = false;
    resp.transfer_id = 0;
    resp.reason = std::string("error: ") + e.what();
    send(fsx::protocol::MsgType::FILE_OFFER_RESP, resp.serialize());
  }
}

void TcpSession::handle_file_accept_req(const std::vector<uint8_t>& payload) {
  if (!is_authenticated()) {
    log("FILE_ACCEPT_REQ rejected: not authenticated");
    fsx::protocol::FileAcceptResp resp;
    resp.ok = false;
    resp.reason = "Not authenticated";
    send(fsx::protocol::MsgType::FILE_ACCEPT_RESP, resp.serialize());
    return;
  }
  
  try {
    fsx::protocol::FileAcceptReq req = fsx::protocol::FileAcceptReq::deserialize(payload);
    
    auto session = transfer_manager_.get_transfer(req.transfer_id);
    if (!session) {
      log("FILE_ACCEPT_REQ FAIL: transfer not found transfer_id=" + std::to_string(req.transfer_id));
      fsx::protocol::FileAcceptResp resp;
      resp.ok = false;
      resp.reason = "Transfer not found";
      send(fsx::protocol::MsgType::FILE_ACCEPT_RESP, resp.serialize());
      return;
    }
    
    // Check if user is the receiver
    if (session->receiver_user_id != user_id_) {
      log("FILE_ACCEPT_REQ FAIL: not the receiver transfer_id=" + std::to_string(req.transfer_id) + 
          " user_id=" + std::to_string(user_id_));
      fsx::protocol::FileAcceptResp resp;
      resp.ok = false;
      resp.reason = "Not the receiver";
      send(fsx::protocol::MsgType::FILE_ACCEPT_RESP, resp.serialize());
      return;
    }
    
    if (req.accept) {
      // Open file for writing
      void* file_handle = file_store_.open_for_write(req.transfer_id, session->filename);
      if (!file_handle) {
        log("FILE_ACCEPT_REQ FAIL: failed to open file transfer_id=" + std::to_string(req.transfer_id));
        transfer_manager_.update_state(req.transfer_id, fsx::transfer::TransferState::FAILED);
        fsx::protocol::FileAcceptResp resp;
        resp.ok = false;
        resp.reason = "Failed to open file";
        send(fsx::protocol::MsgType::FILE_ACCEPT_RESP, resp.serialize());
        return;
      }
      
      session->file_handle = file_handle;
      session->temp_file_path = file_store_.get_temp_path(req.transfer_id, session->filename);
      transfer_manager_.update_state(req.transfer_id, fsx::transfer::TransferState::ACCEPTED);
      
      // Phase 5: Save resume state after accept
      resume_store_.save_resume_state(
        req.transfer_id,
        session->sender_user_id,
        session->receiver_user_id,
        session->filename,
        session->file_size,
        session->chunk_size,
        0,  // No chunks received yet
        0,  // No bytes received yet
        "ACCEPTED",
        session->temp_file_path
      );
      
      log("FILE_ACCEPT_OK transfer_id=" + std::to_string(req.transfer_id) + 
          " receiver=" + username_);
      
      // Notify sender that receiver accepted
      if (!session->sender_token.empty()) {
        log("FILE_ACCEPT: looking for sender session token=" + session->sender_token.substr(0, 8) + "... transfer_id=" + std::to_string(req.transfer_id) + " sender_user_id=" + std::to_string(session->sender_user_id));
        auto sender_session = session_manager_.get_session(session->sender_token);
        if (sender_session) {
          log("FILE_ACCEPT: sender session found, sending FILE_ACCEPT_RESP transfer_id=" + std::to_string(req.transfer_id));
          fsx::protocol::FileAcceptResp sender_resp;
          sender_resp.ok = true;
          sender_session->send(fsx::protocol::MsgType::FILE_ACCEPT_RESP, sender_resp.serialize());
          log("FILE_ACCEPT_RESP sent to sender transfer_id=" + std::to_string(req.transfer_id));
        } else {
          log("FILE_ACCEPT: sender session not found token=" + session->sender_token.substr(0, 8) + "... transfer_id=" + std::to_string(req.transfer_id) + " (sender may have disconnected)");
        }
      } else {
        log("FILE_ACCEPT: sender_token is empty transfer_id=" + std::to_string(req.transfer_id));
      }
    } else {
      transfer_manager_.update_state(req.transfer_id, fsx::transfer::TransferState::FAILED);
      log("FILE_ACCEPT_REJECT transfer_id=" + std::to_string(req.transfer_id) + 
          " receiver=" + username_);
      
      // Notify sender that receiver rejected
      if (!session->sender_token.empty()) {
        auto sender_session = session_manager_.get_session(session->sender_token);
        if (sender_session) {
          fsx::protocol::FileAcceptResp sender_resp;
          sender_resp.ok = false;
          sender_resp.reason = "Receiver rejected";
          sender_session->send(fsx::protocol::MsgType::FILE_ACCEPT_RESP, sender_resp.serialize());
        }
      }
    }
    
    fsx::protocol::FileAcceptResp resp;
    resp.ok = true;
    send(fsx::protocol::MsgType::FILE_ACCEPT_RESP, resp.serialize());
    
  } catch (const std::exception& e) {
    log("FILE_ACCEPT_REQ error: " + std::string(e.what()));
    fsx::protocol::FileAcceptResp resp;
    resp.ok = false;
    resp.reason = std::string("error: ") + e.what();
    send(fsx::protocol::MsgType::FILE_ACCEPT_RESP, resp.serialize());
  }
}

void TcpSession::handle_file_chunk(const std::vector<uint8_t>& payload) {
  log("FILE_CHUNK received (payload_size=" + std::to_string(payload.size()) + ")");
  
  if (!is_authenticated()) {
    log("FILE_CHUNK rejected: not authenticated");
    return;
  }
  
  try {
    fsx::protocol::FileChunk chunk = fsx::protocol::FileChunk::deserialize(payload);
    log("FILE_CHUNK deserialized: transfer_id=" + std::to_string(chunk.transfer_id) + 
        " chunk_index=" + std::to_string(chunk.chunk_index) + 
        " data_size=" + std::to_string(chunk.data.size()));
    
    auto session = transfer_manager_.get_transfer(chunk.transfer_id);
    if (!session) {
      log("FILE_CHUNK FAIL: transfer not found transfer_id=" + std::to_string(chunk.transfer_id));
      return;
    }
    
    // Check if sender is correct
    if (session->sender_user_id != user_id_) {
      log("FILE_CHUNK FAIL: not the sender transfer_id=" + std::to_string(chunk.transfer_id));
      return;
    }
    
    // Check state
    if (session->state != fsx::transfer::TransferState::ACCEPTED && 
        session->state != fsx::transfer::TransferState::RECEIVING) {
      log("FILE_CHUNK FAIL: invalid state transfer_id=" + std::to_string(chunk.transfer_id) + 
          " state=" + std::to_string(static_cast<int>(session->state)));
      return;
    }
    
    // Write chunk to file
    int64_t written = file_store_.write_chunk(session->file_handle, chunk.data.data(), chunk.data.size());
    if (written < 0) {
      log("FILE_CHUNK FAIL: write error transfer_id=" + std::to_string(chunk.transfer_id) + 
          " chunk_index=" + std::to_string(chunk.chunk_index));
      transfer_manager_.update_state(chunk.transfer_id, fsx::transfer::TransferState::FAILED);
      return;
    }
    
    // Mark chunk as received
    transfer_manager_.mark_chunk_received(chunk.transfer_id, chunk.chunk_index, chunk.data.size());
    
    log("FILE_CHUNK_RX transfer_id=" + std::to_string(chunk.transfer_id) + 
        " chunk_index=" + std::to_string(chunk.chunk_index) + 
        " bytes=" + std::to_string(chunk.data.size()) + 
        " total_received=" + std::to_string(session->bytes_received) + 
        "/" + std::to_string(session->file_size));
    
  } catch (const std::exception& e) {
    log("FILE_CHUNK error: " + std::string(e.what()));
  }
}

void TcpSession::handle_file_upload_chunk(const std::vector<uint8_t>& payload) {
  log("FILE_UPLOAD_CHUNK received (payload_size=" + std::to_string(payload.size()) + ")");
  
  if (!is_authenticated()) {
    log("FILE_UPLOAD_CHUNK rejected: not authenticated");
    return;
  }
  
  try {
    fsx::protocol::FileUploadChunk chunk = fsx::protocol::FileUploadChunk::deserialize(payload);
    log("FILE_UPLOAD_CHUNK deserialized: transfer_id=" + std::to_string(chunk.transfer_id) + 
        " chunk_index=" + std::to_string(chunk.chunk_index) + 
        " data_size=" + std::to_string(chunk.data_size) + 
        " crc32=0x" + std::to_string(chunk.crc32) +
        (chunk.original_size > 0 ? " original_size=" + std::to_string(chunk.original_size) + " (compressed)" : ""));
    
    auto session = transfer_manager_.get_transfer(chunk.transfer_id);
    if (!session) {
      log("FILE_UPLOAD_CHUNK FAIL: transfer not found transfer_id=" + std::to_string(chunk.transfer_id));
      return;
    }
    
    // Check if sender is correct
    if (session->sender_user_id != user_id_) {
      log("FILE_UPLOAD_CHUNK FAIL: not the sender transfer_id=" + std::to_string(chunk.transfer_id));
      return;
    }
    
    // Check state
    if (session->state != fsx::transfer::TransferState::ACCEPTED && 
        session->state != fsx::transfer::TransferState::RECEIVING) {
      log("FILE_UPLOAD_CHUNK FAIL: invalid state transfer_id=" + std::to_string(chunk.transfer_id) + 
          " state=" + std::to_string(static_cast<int>(session->state)));
      return;
    }
    
    // Phase 6: If compressed, decompress first; CRC and write apply to uncompressed data
    const std::vector<uint8_t>* data_to_write_ptr = &chunk.data;
    std::vector<uint8_t> decompressed;
    if (chunk.original_size > 0) {
      decompressed = fsx::transfer::ZlibCodec::decompress(chunk.data, chunk.original_size);
      if (decompressed.empty()) {
        log("FILE_UPLOAD_CHUNK FAIL: decompress error transfer_id=" + std::to_string(chunk.transfer_id) +
            " chunk_index=" + std::to_string(chunk.chunk_index));
        fsx::protocol::FileUploadNak nak;
        nak.transfer_id = chunk.transfer_id;
        nak.chunk_index = chunk.chunk_index;
        nak.expected_crc32 = chunk.crc32;
        nak.got_crc32 = 0;
        send(fsx::protocol::MsgType::FILE_UPLOAD_NAK, nak.serialize());
        return;
      }
      data_to_write_ptr = &decompressed;
    }
    
    const std::vector<uint8_t>& data_to_write = *data_to_write_ptr;
    uint32_t calculated_crc = fsx::transfer::IntegrityService::crc32(data_to_write);
    if (calculated_crc != chunk.crc32) {
      log("FILE_UPLOAD_CHUNK CRC32_MISMATCH transfer_id=" + std::to_string(chunk.transfer_id) + 
          " chunk_index=" + std::to_string(chunk.chunk_index) + 
          " expected=0x" + std::to_string(calculated_crc) + 
          " got=0x" + std::to_string(chunk.crc32));
      
      fsx::protocol::FileUploadNak nak;
      nak.transfer_id = chunk.transfer_id;
      nak.chunk_index = chunk.chunk_index;
      nak.expected_crc32 = calculated_crc;
      nak.got_crc32 = chunk.crc32;
      send(fsx::protocol::MsgType::FILE_UPLOAD_NAK, nak.serialize());
      return;
    }
    
    int64_t written = file_store_.write_chunk(session->file_handle, data_to_write.data(), data_to_write.size());
    if (written < 0) {
      log("FILE_UPLOAD_CHUNK FAIL: write error transfer_id=" + std::to_string(chunk.transfer_id) + 
          " chunk_index=" + std::to_string(chunk.chunk_index));
      transfer_manager_.update_state(chunk.transfer_id, fsx::transfer::TransferState::FAILED);
      
      fsx::protocol::FileUploadNak nak;
      nak.transfer_id = chunk.transfer_id;
      nak.chunk_index = chunk.chunk_index;
      nak.expected_crc32 = calculated_crc;
      nak.got_crc32 = chunk.crc32;
      send(fsx::protocol::MsgType::FILE_UPLOAD_NAK, nak.serialize());
      return;
    }
    
    transfer_manager_.mark_chunk_received(chunk.transfer_id, chunk.chunk_index, data_to_write.size());
    
    // Phase 5: Save resume state after successful chunk
    std::string state_str = (session->state == fsx::transfer::TransferState::RECEIVING) ? "RECEIVING" : 
                           (session->state == fsx::transfer::TransferState::ACCEPTED) ? "ACCEPTED" : "UNKNOWN";
    resume_store_.save_resume_state(
      chunk.transfer_id,
      session->sender_user_id,
      session->receiver_user_id,
      session->filename,
      session->file_size,
      session->chunk_size,
      session->expected_chunk_index - 1,  // Last ACKed chunk (expected_chunk_index is next expected)
      session->bytes_received,
      state_str,
      session->temp_file_path
    );
    
    log("FILE_UPLOAD_CHUNK_OK transfer_id=" + std::to_string(chunk.transfer_id) + 
        " chunk_index=" + std::to_string(chunk.chunk_index) + 
        " bytes=" + std::to_string(data_to_write.size()) + 
        " crc32=0x" + std::to_string(chunk.crc32) + 
        " total_received=" + std::to_string(session->bytes_received) + 
        "/" + std::to_string(session->file_size));
    
    // Phase 9: Send ACK (possibly delayed by throttle)
    send_upload_ack_throttled(chunk.transfer_id, chunk.chunk_index, data_to_write.size());
    
  } catch (const std::exception& e) {
    log("FILE_UPLOAD_CHUNK error: " + std::string(e.what()));
  }
}

void TcpSession::handle_file_done(const std::vector<uint8_t>& payload) {
  if (!is_authenticated()) {
    log("FILE_DONE rejected: not authenticated");
    return;
  }
  
  try {
    fsx::protocol::FileDone done = fsx::protocol::FileDone::deserialize(payload);
    
    auto session = transfer_manager_.get_transfer(done.transfer_id);
    if (!session) {
      log("FILE_DONE FAIL: transfer not found transfer_id=" + std::to_string(done.transfer_id));
      return;
    }
    
    // Check if sender is correct
    if (session->sender_user_id != user_id_) {
      log("FILE_DONE FAIL: not the sender transfer_id=" + std::to_string(done.transfer_id));
      return;
    }
    
    // Finalize file
    bool success = file_store_.finalize_file(done.transfer_id, session->filename, session->file_handle);
    if (!success) {
      log("FILE_DONE FAIL: failed to finalize file transfer_id=" + std::to_string(done.transfer_id));
      transfer_manager_.update_state(done.transfer_id, fsx::transfer::TransferState::FAILED);
      
      fsx::protocol::FileResult result;
      result.transfer_id = done.transfer_id;
      result.ok = false;
      result.path_or_reason = "Failed to finalize file";
      send(fsx::protocol::MsgType::FILE_RESULT, result.serialize());
      return;
    }
    
    transfer_manager_.update_state(done.transfer_id, fsx::transfer::TransferState::COMPLETED);
    
    // Phase 5: Clear resume state after completion
    resume_store_.clear_resume_state(done.transfer_id);
    
    // Phase 7: SHA-256 integrity — compute hash of saved file for UI/log and optional verification
    std::vector<uint8_t> computed_sha256 = fsx::transfer::IntegrityService::sha256_file(session->final_file_path);
    int sha256_status = -1;  // -1 = not sent, 0 = not_checked (client didn't send), 1 = verified, 2 = mismatch
    if (!computed_sha256.empty()) {
      sha256_status = 0;
      if (done.file_sha256.size() == fsx::transfer::IntegrityService::SHA256_SIZE) {
        sha256_status = fsx::transfer::IntegrityService::sha256_equal(computed_sha256, done.file_sha256) ? 1 : 2;
      }
    } else {
      log("FILE_DONE WARN: SHA-256 computation failed for transfer_id=" + std::to_string(done.transfer_id));
    }
    
    std::string sha256_log = (sha256_status == 1) ? " sha256=verified" :
                             (sha256_status == 2) ? " sha256=mismatch" :
                             (sha256_status == 0) ? " sha256=computed_only" : "";
    log("FILE_DONE_OK transfer_id=" + std::to_string(done.transfer_id) + 
        " filename=" + session->filename + 
        " total_chunks=" + std::to_string(done.total_chunks) + 
        " file_size=" + std::to_string(done.file_size) + 
        " saved_path=" + session->final_file_path + sha256_log);
    
    fsx::protocol::FileResult result;
    result.transfer_id = done.transfer_id;
    result.ok = true;
    result.path_or_reason = session->final_file_path;
    if (sha256_status >= 0) {
      result.sha256_status = sha256_status;
      result.computed_sha256 = std::move(computed_sha256);
    }
    send(fsx::protocol::MsgType::FILE_RESULT, result.serialize());
    
  } catch (const std::exception& e) {
    log("FILE_DONE error: " + std::string(e.what()));
  }
}

void TcpSession::handle_resume_query(const std::vector<uint8_t>& payload) {
  log("RESUME_QUERY received");
  
  if (!is_authenticated()) {
    log("RESUME_QUERY rejected: not authenticated");
    fsx::protocol::ResumeReply reply;
    reply.can_resume = false;
    reply.transfer_id = 0;
    reply.reason = "Not authenticated";
    send(fsx::protocol::MsgType::RESUME_REPLY, reply.serialize());
    return;
  }
  
  try {
    fsx::protocol::ResumeQuery query = fsx::protocol::ResumeQuery::deserialize(payload);
    log("RESUME_QUERY transfer_id=" + std::to_string(query.transfer_id) + 
        " from user_id=" + std::to_string(user_id_));
    
    // Check if transfer can be resumed
    auto resume_state = resume_store_.get_resume_state(query.transfer_id);
    
    fsx::protocol::ResumeReply reply;
    reply.transfer_id = query.transfer_id;
    
    if (!resume_state.has_value()) {
      // Cannot resume
      reply.can_resume = false;
      reply.reason = "Transfer not found or cannot be resumed";
      log("RESUME_QUERY FAIL transfer_id=" + std::to_string(query.transfer_id) + 
          " reason=" + reply.reason);
    } else {
      // Check if user is authorized: must be the SENDER (sender resumes upload)
      if (resume_state->sender_user_id != user_id_) {
        reply.can_resume = false;
        reply.reason = "Not authorized to resume this transfer";
        log("RESUME_QUERY FAIL transfer_id=" + std::to_string(query.transfer_id) + 
            " user_id=" + std::to_string(user_id_) + 
            " expected_sender=" + std::to_string(resume_state->sender_user_id));
      } else {
        // Can resume
        reply.can_resume = true;
        reply.filename = resume_state->filename;
        reply.file_size = resume_state->file_size;
        reply.chunk_size = resume_state->chunk_size;
        reply.last_acked_chunk_index = resume_state->last_acked_chunk_index;
        reply.bytes_received = resume_state->bytes_received;
        
        log("RESUME_QUERY OK transfer_id=" + std::to_string(query.transfer_id) + 
            " filename=" + reply.filename + 
            " last_acked_chunk=" + std::to_string(reply.last_acked_chunk_index) + 
            " bytes_received=" + std::to_string(reply.bytes_received));
      }
    }
    
    send(fsx::protocol::MsgType::RESUME_REPLY, reply.serialize());
    
  } catch (const std::exception& e) {
    log("RESUME_QUERY error: " + std::string(e.what()));
    fsx::protocol::ResumeReply reply;
    reply.can_resume = false;
    reply.transfer_id = 0;
    reply.reason = std::string("Error: ") + e.what();
    send(fsx::protocol::MsgType::RESUME_REPLY, reply.serialize());
  }
}

void TcpSession::handle_key_exchange_session_key(const std::vector<uint8_t>& payload) {
  if (payload.empty()) {
    log("KEY_EXCHANGE_SESSION_KEY rejected: empty payload");
    return;
  }
  auto key = rsa_keypair_.decrypt(payload);
  if (key.size() != fsx::crypto::AES256_KEY_SIZE) {
    log("KEY_EXCHANGE_SESSION_KEY FAIL: decrypt size=" + std::to_string(key.size()) + " (expected 32)");
    return;
  }
  session_key_ = std::move(key);
  log("KEY_EXCHANGE_OK session_key set (32 bytes) from=" + get_remote_endpoint());
}

// -------- Phase 9 handlers --------

void TcpSession::send_upload_ack_throttled(uint64_t transfer_id, uint32_t chunk_index,
                                           size_t chunk_bytes) {
  // Record bytes for speed measurement
  transfer_manager_.record_transfer_bytes(user_id_, chunk_bytes);

  // Calculate throttle delay (global + per-user)
  uint32_t delay_ms = transfer_manager_.get_throttle_delay(user_id_, chunk_bytes);

  // Build ACK payload
  fsx::protocol::FileUploadAck ack;
  ack.transfer_id = transfer_id;
  ack.chunk_index = chunk_index;
  auto ack_payload = ack.serialize();

  if (delay_ms > 0) {
    // Delayed ACK via async timer
    auto timer = std::make_shared<boost::asio::steady_timer>(
        socket_.get_executor(),
        std::chrono::milliseconds(delay_ms));
    auto self = shared_from_this();
    uint32_t dms = delay_ms;
    timer->async_wait([this, self, timer, ack_payload, dms](boost::system::error_code ec) {
      if (!ec) {
        send(fsx::protocol::MsgType::FILE_UPLOAD_ACK, ack_payload);
        log("THROTTLE_DELAYED_ACK delay=" + std::to_string(dms) + "ms");
      }
    });
  } else {
    send(fsx::protocol::MsgType::FILE_UPLOAD_ACK, ack_payload);
  }
}

void TcpSession::handle_throttle_set(const std::vector<uint8_t>& payload) {
  log("RECV THROTTLE_SET from=" + get_remote_endpoint());

  // Payload: u8 scope (0=global, 1=per-user) + [u64 user_id if scope=1] + u64 bps
  if (payload.size() < 9) {
    log("THROTTLE_SET rejected: payload too short");
    return;
  }

  size_t pos = 0;
  uint8_t scope = payload[pos]; pos += 1;
  uint64_t user_id = 0;
  if (scope == 1) {
    if (payload.size() < 17) {
      log("THROTTLE_SET rejected: payload too short for per-user");
      return;
    }
    std::memcpy(&user_id, payload.data() + pos, 8);
    user_id = be64toh(user_id);
    pos += 8;
  }

  uint64_t bps = 0;
  std::memcpy(&bps, payload.data() + pos, 8);
  bps = be64toh(bps);

  if (scope == 0) {
    transfer_manager_.set_global_throttle(bps);
    log("THROTTLE_SET global=" + std::to_string(bps) + " bytes/sec" +
        (bps == 0 ? " (unlimited)" : ""));
  } else {
    transfer_manager_.set_user_throttle(static_cast<long long>(user_id), bps);
    log("THROTTLE_SET user_id=" + std::to_string(user_id) + " rate=" + std::to_string(bps) +
        " bytes/sec" + (bps == 0 ? " (unlimited)" : ""));
  }

  // Send response
  std::vector<uint8_t> resp;
  resp.push_back(1); // ok
  uint64_t bps_be = htobe64(bps);
  resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&bps_be),
              reinterpret_cast<const uint8_t*>(&bps_be) + 8);
  send(fsx::protocol::MsgType::THROTTLE_SET_RESP, resp);
}

void TcpSession::handle_transfer_list_req(const std::vector<uint8_t>& /* payload */) {
  log("RECV TRANSFER_LIST_REQ from=" + get_remote_endpoint());

  auto transfers = transfer_manager_.get_all_transfers();

  // Build response payload
  std::vector<uint8_t> resp;

  // count (u16)
  uint16_t count = static_cast<uint16_t>(transfers.size());
  uint16_t count_be = htons(count);
  resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&count_be),
              reinterpret_cast<const uint8_t*>(&count_be) + 2);

  for (const auto& t : transfers) {
    // transfer_id (u64)
    uint64_t tid_be = htobe64(t->transfer_id);
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&tid_be),
                reinterpret_cast<const uint8_t*>(&tid_be) + 8);

    // state (u8)
    resp.push_back(static_cast<uint8_t>(t->state));

    // sender username (u16 len + str)
    uint16_t slen = htons(static_cast<uint16_t>(t->sender_username.size()));
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&slen),
                reinterpret_cast<const uint8_t*>(&slen) + 2);
    resp.insert(resp.end(), t->sender_username.begin(), t->sender_username.end());

    // receiver username (u16 len + str)
    uint16_t rlen = htons(static_cast<uint16_t>(t->receiver_username.size()));
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&rlen),
                reinterpret_cast<const uint8_t*>(&rlen) + 2);
    resp.insert(resp.end(), t->receiver_username.begin(), t->receiver_username.end());

    // filename (u16 len + str)
    uint16_t flen = htons(static_cast<uint16_t>(t->filename.size()));
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&flen),
                reinterpret_cast<const uint8_t*>(&flen) + 2);
    resp.insert(resp.end(), t->filename.begin(), t->filename.end());

    // file_size (u64)
    uint64_t fsz_be = htobe64(t->file_size);
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&fsz_be),
                reinterpret_cast<const uint8_t*>(&fsz_be) + 8);

    // bytes_received (u64)
    uint64_t br_be = htobe64(t->bytes_received);
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&br_be),
                reinterpret_cast<const uint8_t*>(&br_be) + 8);

    // speed (u64) — elapsed based
    auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t->start_time).count();
    uint64_t speed = elapsed > 0.5 ?
        static_cast<uint64_t>(t->bytes_received / elapsed) : 0;
    uint64_t speed_be = htobe64(speed);
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&speed_be),
                reinterpret_cast<const uint8_t*>(&speed_be) + 8);
  }

  send(fsx::protocol::MsgType::TRANSFER_LIST_RESP, resp);
  log("TRANSFER_LIST_RESP count=" + std::to_string(count));
}

// -------- Phase 10: Voice chat signaling --------

void TcpSession::handle_voice_call_req(const std::vector<uint8_t>& payload) {
  if (!is_authenticated()) {
    log("VOICE_CALL_REQ rejected: not authenticated");
    // Send reject response
    std::vector<uint8_t> resp(7, 0);
    resp[0] = 0; // not ok
    send(fsx::protocol::MsgType::VOICE_CALL_RESP, resp);
    return;
  }
  if (!voice_manager_) {
    log("VOICE_CALL_REQ rejected: voice not enabled");
    std::vector<uint8_t> resp(7, 0);
    resp[0] = 0;
    send(fsx::protocol::MsgType::VOICE_CALL_RESP, resp);
    return;
  }

  // Payload: u16 callee_username_len + callee_username
  if (payload.size() < 2) {
    log("VOICE_CALL_REQ rejected: payload too short");
    return;
  }
  uint16_t callee_len_be;
  std::memcpy(&callee_len_be, payload.data(), 2);
  uint16_t callee_len = ntohs(callee_len_be);
  if (payload.size() < 2 + callee_len) {
    log("VOICE_CALL_REQ rejected: payload truncated");
    return;
  }
  std::string callee_name(payload.begin() + 2, payload.begin() + 2 + callee_len);

  log("VOICE_CALL_REQ caller=" + username_ + " callee=" + callee_name);

  // Cannot call yourself
  if (callee_name == username_) {
    log("VOICE_CALL_REQ rejected: self-call");
    std::vector<uint8_t> resp(7, 0);
    resp[0] = 0;
    send(fsx::protocol::MsgType::VOICE_CALL_RESP, resp);
    return;
  }

  // Find callee session
  auto callee_session = session_manager_.get_session_by_username(callee_name);
  if (!callee_session) {
    log("VOICE_CALL_REQ rejected: callee not online callee=" + callee_name);
    std::vector<uint8_t> resp(7, 0);
    resp[0] = 0;
    send(fsx::protocol::MsgType::VOICE_CALL_RESP, resp);
    return;
  }

  // Create voice session
  uint32_t session_id = voice_manager_->start_session(
      user_id_, username_, callee_session->user_id(), callee_name);

  // Remember this session on the caller side
  active_voice_session_id_ = session_id;

  // Send VOICE_CALL_NOTIFY to callee:
  //   u32 session_id + u16 caller_name_len + caller_name
  {
    std::vector<uint8_t> notify;
    uint32_t sid_be = htonl(session_id);
    notify.insert(notify.end(), reinterpret_cast<const uint8_t*>(&sid_be),
                  reinterpret_cast<const uint8_t*>(&sid_be) + 4);
    uint16_t clen_be = htons(static_cast<uint16_t>(username_.size()));
    notify.insert(notify.end(), reinterpret_cast<const uint8_t*>(&clen_be),
                  reinterpret_cast<const uint8_t*>(&clen_be) + 2);
    notify.insert(notify.end(), username_.begin(), username_.end());

    callee_session->send(fsx::protocol::MsgType::VOICE_CALL_NOTIFY, notify);
    // Set pending call on callee session
    callee_session->pending_voice_session_id_ = session_id;
    callee_session->pending_voice_caller_ = username_;
  }

  log("VOICE_CALL_NOTIFY sent to callee=" + callee_name + " session_id=" + std::to_string(session_id));
}

void TcpSession::handle_voice_call_resp(const std::vector<uint8_t>& payload) {
  // Callee responds to an incoming call
  // Payload: u8 ok + u32 session_id
  if (payload.size() < 5) {
    log("VOICE_CALL_RESP rejected: payload too short");
    return;
  }

  uint8_t ok = payload[0];
  uint32_t session_id_be;
  std::memcpy(&session_id_be, payload.data() + 1, 4);
  uint32_t session_id = ntohl(session_id_be);

  log("VOICE_CALL_RESP from=" + username_ + " session_id=" + std::to_string(session_id) +
      " ok=" + std::to_string(ok));

  if (!voice_manager_) return;

  auto vs = voice_manager_->get_session(session_id);
  if (!vs) {
    log("VOICE_CALL_RESP rejected: session not found session_id=" + std::to_string(session_id));
    return;
  }

  if (ok) {
    // Callee accepted — notify caller
    active_voice_session_id_ = session_id;
    pending_voice_session_id_ = 0;
    pending_voice_caller_.clear();

    // Find caller session and send VOICE_CALL_RESP
    auto caller_session = session_manager_.get_session_by_username(vs->caller_username);
    if (caller_session) {
      // Payload to caller: u8 ok(1) + u32 session_id + u16 udp_port
      std::vector<uint8_t> resp;
      resp.push_back(1); // ok
      uint32_t sid_be = htonl(session_id);
      resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&sid_be),
                  reinterpret_cast<const uint8_t*>(&sid_be) + 4);
      uint16_t port_be = htons(udp_voice_port_);
      resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&port_be),
                  reinterpret_cast<const uint8_t*>(&port_be) + 2);
      caller_session->send(fsx::protocol::MsgType::VOICE_CALL_RESP, resp);
      log("VOICE_CALL_RESP(accepted) sent to caller=" + vs->caller_username);
    }

    // Also send the accepted response back to callee (this session) with session_id + port
    {
      std::vector<uint8_t> callee_resp;
      callee_resp.push_back(1);
      uint32_t sid_be = htonl(session_id);
      callee_resp.insert(callee_resp.end(), reinterpret_cast<const uint8_t*>(&sid_be),
                         reinterpret_cast<const uint8_t*>(&sid_be) + 4);
      uint16_t port_be = htons(udp_voice_port_);
      callee_resp.insert(callee_resp.end(), reinterpret_cast<const uint8_t*>(&port_be),
                         reinterpret_cast<const uint8_t*>(&port_be) + 2);
      send(fsx::protocol::MsgType::VOICE_CALL_RESP, callee_resp);
    }

    log("VOICE_CALL session_id=" + std::to_string(session_id) + " ACTIVE between " +
        vs->caller_username + " and " + vs->callee_username);
  } else {
    // Callee rejected — end session and notify caller
    pending_voice_session_id_ = 0;
    pending_voice_caller_.clear();

    auto caller_session = session_manager_.get_session_by_username(vs->caller_username);
    if (caller_session) {
      std::vector<uint8_t> resp(7, 0);
      resp[0] = 0; // rejected
      uint32_t sid_be = htonl(session_id);
      std::memcpy(resp.data() + 1, &sid_be, 4);
      caller_session->send(fsx::protocol::MsgType::VOICE_CALL_RESP, resp);
      caller_session->active_voice_session_id_ = 0;
    }
    voice_manager_->end_session(session_id);
    log("VOICE_CALL session_id=" + std::to_string(session_id) + " REJECTED by " + username_);
  }
}

void TcpSession::handle_voice_end(const std::vector<uint8_t>& payload) {
  // Payload: u32 session_id
  if (payload.size() < 4) {
    log("VOICE_END rejected: payload too short");
    return;
  }

  uint32_t session_id_be;
  std::memcpy(&session_id_be, payload.data(), 4);
  uint32_t session_id = ntohl(session_id_be);

  log("VOICE_END from=" + username_ + " session_id=" + std::to_string(session_id));

  if (!voice_manager_) return;

  auto vs = voice_manager_->get_session(session_id);
  if (!vs) {
    log("VOICE_END: session not found (already ended?)");
    active_voice_session_id_ = 0;
    return;
  }

  // Determine the other party and notify them
  std::string other_username;
  if (vs->caller_username == username_) {
    other_username = vs->callee_username;
  } else {
    other_username = vs->caller_username;
  }

  auto other_session = session_manager_.get_session_by_username(other_username);
  if (other_session) {
    // Forward VOICE_END to the other side
    std::vector<uint8_t> end_payload;
    uint32_t sid_be = htonl(session_id);
    end_payload.insert(end_payload.end(), reinterpret_cast<const uint8_t*>(&sid_be),
                       reinterpret_cast<const uint8_t*>(&sid_be) + 4);
    other_session->send(fsx::protocol::MsgType::VOICE_END, end_payload);
    other_session->active_voice_session_id_ = 0;
    log("VOICE_END sent to " + other_username);
  }

  active_voice_session_id_ = 0;
  voice_manager_->end_session(session_id);
}

// -------- Phase 11: Voice session list (admin/dashboard) --------

void TcpSession::handle_voice_session_list_req(const std::vector<uint8_t>& /* payload */) {
  log("RECV VOICE_SESSION_LIST_REQ from=" + get_remote_endpoint());

  std::vector<uint8_t> resp;

  if (!voice_manager_) {
    // No voice manager — return count=0
    uint16_t zero = 0;
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&zero),
                reinterpret_cast<const uint8_t*>(&zero) + 2);
    send(fsx::protocol::MsgType::VOICE_SESSION_LIST_RESP, resp);
    return;
  }

  auto sessions = voice_manager_->get_all_sessions();
  uint16_t count_be = htons(static_cast<uint16_t>(sessions.size()));
  resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&count_be),
              reinterpret_cast<const uint8_t*>(&count_be) + 2);

  for (auto& s : sessions) {
    // session_id (u32)
    uint32_t sid_be = htonl(s->session_id);
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&sid_be),
                reinterpret_cast<const uint8_t*>(&sid_be) + 4);

    // caller username (u16 len + str)
    uint16_t clen = htons(static_cast<uint16_t>(s->caller_username.size()));
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&clen),
                reinterpret_cast<const uint8_t*>(&clen) + 2);
    resp.insert(resp.end(), s->caller_username.begin(), s->caller_username.end());

    // callee username (u16 len + str)
    uint16_t elen = htons(static_cast<uint16_t>(s->callee_username.size()));
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&elen),
                reinterpret_cast<const uint8_t*>(&elen) + 2);
    resp.insert(resp.end(), s->callee_username.begin(), s->callee_username.end());

    // frames_relayed (u64)
    uint64_t fr_be = htobe64(s->frames_relayed);
    resp.insert(resp.end(), reinterpret_cast<const uint8_t*>(&fr_be),
                reinterpret_cast<const uint8_t*>(&fr_be) + 8);
  }

  send(fsx::protocol::MsgType::VOICE_SESSION_LIST_RESP, resp);
  log("VOICE_SESSION_LIST_RESP count=" + std::to_string(sessions.size()));
}

} // namespace fsx::net
