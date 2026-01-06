#include "fsx/net/tcp_session.h"
#include "fsx/net/session_manager.h"
#include "fsx/protocol/auth_messages.h"
#include "fsx/protocol/online_messages.h"
#include "fsx/protocol/file_messages.h"
#include "fsx/transfer/transfer_manager.h"
#include "fsx/storage/file_store.h"
#include "fsx/db/user_repository.h"
#include <chrono>
#include <sstream>
#include <cstdio>

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
                       db::UserRepository& user_repository)
  : socket_(std::move(socket)), 
    auth_handler_(auth_handler), 
    session_manager_(session_manager),
    transfer_manager_(transfer_manager),
    file_store_(file_store),
    user_repository_(user_repository) {}

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
  
  if (type == fsx::protocol::MsgType::FILE_DONE) {
    handle_file_done(payload);
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
      transfer_manager_.update_state(req.transfer_id, fsx::transfer::TransferState::ACCEPTED);
      
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
    
    log("FILE_DONE_OK transfer_id=" + std::to_string(done.transfer_id) + 
        " filename=" + session->filename + 
        " total_chunks=" + std::to_string(done.total_chunks) + 
        " file_size=" + std::to_string(done.file_size) + 
        " saved_path=" + session->final_file_path);
    
    fsx::protocol::FileResult result;
    result.transfer_id = done.transfer_id;
    result.ok = true;
    result.path_or_reason = session->final_file_path;
    send(fsx::protocol::MsgType::FILE_RESULT, result.serialize());
    
  } catch (const std::exception& e) {
    log("FILE_DONE error: " + std::string(e.what()));
  }
}

} // namespace fsx::net
