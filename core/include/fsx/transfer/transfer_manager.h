#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>
#include <chrono>
#include "fsx/transfer/throttler.h"

namespace fsx::transfer {

enum class TransferState {
  OFFERED,    // Transfer offered, waiting for accept
  ACCEPTED,   // Accepted by receiver, ready for chunks
  RECEIVING,  // Receiving chunks
  COMPLETED,  // All chunks received and file saved
  FAILED      // Transfer failed
};

struct TransferSession {
  uint64_t transfer_id = 0;
  long long sender_user_id = 0;
  std::string sender_username;
  std::string sender_token;  // Token to find sender session
  long long receiver_user_id = 0;
  std::string receiver_username;
  std::string filename;
  uint64_t file_size = 0;
  uint32_t chunk_size = 0;
  uint32_t expected_chunk_index = 0;  // Next expected chunk (0-based)
  uint64_t bytes_received = 0;
  TransferState state = TransferState::OFFERED;
  std::string temp_file_path;  // .part file path
  std::string final_file_path; // Final file path after completion
  
  // File handle (will be managed by FileStore)
  void* file_handle = nullptr;  // FILE* cast to void* for portability

  // Phase 9: timing for speed measurement
  std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
};

class TransferManager {
public:
  TransferManager();
  ~TransferManager();

  // Generate a new unique transfer ID
  uint64_t generate_transfer_id();

  // Create a new transfer session (from FILE_OFFER)
  uint64_t create_transfer(
    long long sender_user_id,
    const std::string& sender_username,
    const std::string& sender_token,
    long long receiver_user_id,
    const std::string& receiver_username,
    const std::string& filename,
    uint64_t file_size,
    uint32_t chunk_size
  );

  std::shared_ptr<TransferSession> get_transfer(uint64_t transfer_id);
  bool update_state(uint64_t transfer_id, TransferState new_state);
  bool mark_chunk_received(uint64_t transfer_id, uint32_t chunk_index, size_t chunk_bytes);
  bool remove_transfer(uint64_t transfer_id);
  std::vector<std::shared_ptr<TransferSession>> get_all_transfers();

  // ---- Phase 9: Throttle API ----

  /// Set global rate limit (0 = unlimited).
  void set_global_throttle(uint64_t bytes_per_second);
  uint64_t get_global_throttle() const;

  /// Set per-user rate limit (0 = unlimited / remove).
  void set_user_throttle(long long user_id, uint64_t bytes_per_second);
  uint64_t get_user_throttle(long long user_id) const;

  /**
   * Consume `bytes` through the combined global + per-user throttle.
   * @return delay (ms) the caller must wait before acking.
   */
  uint32_t get_throttle_delay(long long user_id, size_t bytes);

  /// Record bytes for per-user speed measurement.
  void record_transfer_bytes(long long user_id, size_t bytes);

  /// Get measured speed for a user.
  uint64_t get_user_speed(long long user_id) const;

  /// Get all per-user throttle settings: {user_id, bps}.
  std::vector<std::pair<long long, uint64_t>> get_all_user_throttles() const;

private:
  std::atomic<uint64_t> next_transfer_id_{1};
  std::unordered_map<uint64_t, std::shared_ptr<TransferSession>> transfers_;
  mutable std::mutex mutex_;

  // Phase 9: throttling
  Throttler global_throttler_;
  mutable std::mutex throttle_mutex_;
  std::unordered_map<long long, std::shared_ptr<Throttler>> user_throttlers_;

  std::shared_ptr<Throttler> get_or_create_user_throttler(long long user_id);
};

} // namespace fsx::transfer

