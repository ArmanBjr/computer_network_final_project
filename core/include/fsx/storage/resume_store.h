#pragma once

#include "fsx/db/db.h"
#include <cstdint>
#include <string>
#include <optional>

namespace fsx::storage {

struct ResumeState {
  uint64_t transfer_id = 0;
  long long sender_user_id = 0;
  long long receiver_user_id = 0;
  std::string filename;
  uint64_t file_size = 0;
  uint32_t chunk_size = 0;
  uint32_t last_acked_chunk_index = 0;  // Last successfully received chunk (0-based)
  uint64_t bytes_received = 0;
  std::string state;  // OFFERED, ACCEPTED, RECEIVING, COMPLETED, FAILED
  std::string temp_file_path;
};

class ResumeStore {
public:
  explicit ResumeStore(fsx::db::Db& db) : db_(db) {}

  // Ensure transfer_resume table exists (for DBs created before Phase 5)
  void ensure_table();

  // Save resume state for a transfer
  // Called after each chunk is successfully received
  bool save_resume_state(
    uint64_t transfer_id,
    long long sender_user_id,
    long long receiver_user_id,
    const std::string& filename,
    uint64_t file_size,
    uint32_t chunk_size,
    uint32_t last_acked_chunk_index,
    uint64_t bytes_received,
    const std::string& state,
    const std::string& temp_file_path
  );

  // Get resume state for a transfer
  // Returns nullopt if transfer cannot be resumed
  std::optional<ResumeState> get_resume_state(uint64_t transfer_id);

  // Clear resume state (after transfer completes or fails)
  bool clear_resume_state(uint64_t transfer_id);

  // Check if a transfer can be resumed
  bool can_resume(uint64_t transfer_id);

private:
  fsx::db::Db& db_;
  
  std::string state_to_string(const std::string& state) const;
};

} // namespace fsx::storage
