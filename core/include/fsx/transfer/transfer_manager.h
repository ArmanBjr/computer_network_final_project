#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <vector>

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
};

class TransferManager {
public:
  TransferManager();
  ~TransferManager();

  // Generate a new unique transfer ID
  uint64_t generate_transfer_id();

  // Create a new transfer session (from FILE_OFFER)
  // Returns transfer_id on success, 0 on failure
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

  // Get transfer session (returns nullptr if not found)
  std::shared_ptr<TransferSession> get_transfer(uint64_t transfer_id);

  // Update transfer state
  bool update_state(uint64_t transfer_id, TransferState new_state);

  // Mark chunk as received (increment expected_chunk_index)
  bool mark_chunk_received(uint64_t transfer_id, uint32_t chunk_index, size_t chunk_bytes);

  // Remove transfer (cleanup)
  bool remove_transfer(uint64_t transfer_id);

  // Get all active transfers (for monitoring)
  std::vector<std::shared_ptr<TransferSession>> get_all_transfers();

private:
  std::atomic<uint64_t> next_transfer_id_{1};
  std::unordered_map<uint64_t, std::shared_ptr<TransferSession>> transfers_;
  std::mutex mutex_;
};

} // namespace fsx::transfer

