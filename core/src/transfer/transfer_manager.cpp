#include "fsx/transfer/transfer_manager.h"
#include <algorithm>

namespace fsx::transfer {

TransferManager::TransferManager() {
  next_transfer_id_.store(1);
}

TransferManager::~TransferManager() {
  std::lock_guard<std::mutex> lock(mutex_);
  transfers_.clear();
}

uint64_t TransferManager::generate_transfer_id() {
  return next_transfer_id_.fetch_add(1);
}

uint64_t TransferManager::create_transfer(
  long long sender_user_id,
  const std::string& sender_username,
  const std::string& sender_token,
  long long receiver_user_id,
  const std::string& receiver_username,
  const std::string& filename,
  uint64_t file_size,
  uint32_t chunk_size
) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  uint64_t transfer_id = generate_transfer_id();
  
  auto session = std::make_shared<TransferSession>();
  session->transfer_id = transfer_id;
  session->sender_user_id = sender_user_id;
  session->sender_username = sender_username;
  session->sender_token = sender_token;
  session->receiver_user_id = receiver_user_id;
  session->receiver_username = receiver_username;
  session->filename = filename;
  session->file_size = file_size;
  session->chunk_size = chunk_size;
  session->state = TransferState::OFFERED;
  session->expected_chunk_index = 0;
  session->bytes_received = 0;
  
  transfers_[transfer_id] = session;
  
  return transfer_id;
}

std::shared_ptr<TransferSession> TransferManager::get_transfer(uint64_t transfer_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = transfers_.find(transfer_id);
  if (it != transfers_.end()) {
    return it->second;
  }
  return nullptr;
}

bool TransferManager::update_state(uint64_t transfer_id, TransferState new_state) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = transfers_.find(transfer_id);
  if (it != transfers_.end()) {
    it->second->state = new_state;
    return true;
  }
  return false;
}

bool TransferManager::mark_chunk_received(uint64_t transfer_id, uint32_t chunk_index, size_t chunk_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = transfers_.find(transfer_id);
  if (it != transfers_.end()) {
    auto& session = it->second;
    // For MVP: simple sequential check (Phase 4 will add proper validation)
    if (chunk_index == session->expected_chunk_index) {
      session->expected_chunk_index++;
      session->bytes_received += chunk_bytes;
      if (session->state == TransferState::ACCEPTED) {
        session->state = TransferState::RECEIVING;
      }
      return true;
    }
    // Out of order chunk (will be handled in Phase 4 with retransmission)
    return false;
  }
  return false;
}

bool TransferManager::remove_transfer(uint64_t transfer_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  return transfers_.erase(transfer_id) > 0;
}

std::vector<std::shared_ptr<TransferSession>> TransferManager::get_all_transfers() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::shared_ptr<TransferSession>> result;
  result.reserve(transfers_.size());
  for (const auto& pair : transfers_) {
    result.push_back(pair.second);
  }
  return result;
}

} // namespace fsx::transfer

