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
  session->start_time = std::chrono::steady_clock::now();
  
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
    if (chunk_index == session->expected_chunk_index) {
      session->expected_chunk_index++;
      session->bytes_received += chunk_bytes;
      if (session->state == TransferState::ACCEPTED) {
        session->state = TransferState::RECEIVING;
      }
      return true;
    }
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

// ---- Phase 9: Throttle implementation ----

void TransferManager::set_global_throttle(uint64_t bytes_per_second) {
  global_throttler_.set_rate(bytes_per_second);
}

uint64_t TransferManager::get_global_throttle() const {
  return global_throttler_.get_rate();
}

void TransferManager::set_user_throttle(long long user_id, uint64_t bytes_per_second) {
  std::lock_guard<std::mutex> lock(throttle_mutex_);
  if (bytes_per_second == 0) {
    user_throttlers_.erase(user_id);
    return;
  }
  auto it = user_throttlers_.find(user_id);
  if (it != user_throttlers_.end()) {
    it->second->set_rate(bytes_per_second);
  } else {
    auto t = std::make_shared<Throttler>();
    t->set_rate(bytes_per_second);
    user_throttlers_[user_id] = t;
  }
}

uint64_t TransferManager::get_user_throttle(long long user_id) const {
  std::lock_guard<std::mutex> lock(throttle_mutex_);
  auto it = user_throttlers_.find(user_id);
  if (it != user_throttlers_.end()) {
    return it->second->get_rate();
  }
  return 0;
}

std::shared_ptr<Throttler> TransferManager::get_or_create_user_throttler(long long user_id) {
  // caller must hold throttle_mutex_ if needed, but here we lock ourselves
  auto it = user_throttlers_.find(user_id);
  if (it != user_throttlers_.end()) return it->second;
  auto t = std::make_shared<Throttler>();
  user_throttlers_[user_id] = t;
  return t;
}

uint32_t TransferManager::get_throttle_delay(long long user_id, size_t bytes) {
  // Global throttle
  uint32_t global_delay = global_throttler_.consume(bytes);

  // Per-user throttle
  uint32_t user_delay = 0;
  {
    std::lock_guard<std::mutex> lock(throttle_mutex_);
    auto it = user_throttlers_.find(user_id);
    if (it != user_throttlers_.end()) {
      user_delay = it->second->consume(bytes);
    }
  }

  // Use the larger delay (stricter limit wins)
  return std::max(global_delay, user_delay);
}

void TransferManager::record_transfer_bytes(long long user_id, size_t bytes) {
  global_throttler_.record_bytes(bytes);
  {
    std::lock_guard<std::mutex> lock(throttle_mutex_);
    auto it = user_throttlers_.find(user_id);
    if (it != user_throttlers_.end()) {
      it->second->record_bytes(bytes);
    }
  }
}

uint64_t TransferManager::get_user_speed(long long user_id) const {
  std::lock_guard<std::mutex> lock(throttle_mutex_);
  auto it = user_throttlers_.find(user_id);
  if (it != user_throttlers_.end()) {
    return it->second->get_current_speed();
  }
  return 0;
}

std::vector<std::pair<long long, uint64_t>> TransferManager::get_all_user_throttles() const {
  std::lock_guard<std::mutex> lock(throttle_mutex_);
  std::vector<std::pair<long long, uint64_t>> result;
  result.reserve(user_throttlers_.size());
  for (const auto& [uid, t] : user_throttlers_) {
    result.emplace_back(uid, t->get_rate());
  }
  return result;
}

} // namespace fsx::transfer

