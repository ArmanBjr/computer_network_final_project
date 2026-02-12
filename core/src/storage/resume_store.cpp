#include "fsx/storage/resume_store.h"
#include "fsx/db/db.h"
#include <libpq-fe.h>
#include <stdexcept>
#include <sstream>

namespace fsx::storage {

void ResumeStore::ensure_table() {
  PGresult* r = db_.exec(
    "CREATE TABLE IF NOT EXISTS transfer_resume ("
    "  id BIGSERIAL PRIMARY KEY, transfer_id BIGINT NOT NULL UNIQUE,"
    "  sender_user_id BIGINT NOT NULL, receiver_user_id BIGINT NOT NULL,"
    "  filename TEXT NOT NULL, file_size BIGINT NOT NULL, chunk_size INTEGER NOT NULL,"
    "  last_acked_chunk_index INTEGER NOT NULL DEFAULT 0, bytes_received BIGINT NOT NULL DEFAULT 0,"
    "  state TEXT NOT NULL DEFAULT 'RECEIVING', temp_file_path TEXT,"
    "  created_at TIMESTAMPTZ NOT NULL DEFAULT now(), updated_at TIMESTAMPTZ NOT NULL DEFAULT now()"
    ")");
  if (r) { PQclear(r); }
  r = db_.exec("CREATE INDEX IF NOT EXISTS idx_transfer_resume_transfer_id ON transfer_resume(transfer_id)");
  if (r) { PQclear(r); }
  r = db_.exec("CREATE INDEX IF NOT EXISTS idx_transfer_resume_sender ON transfer_resume(sender_user_id)");
  if (r) { PQclear(r); }
  r = db_.exec("CREATE INDEX IF NOT EXISTS idx_transfer_resume_receiver ON transfer_resume(receiver_user_id)");
  if (r) { PQclear(r); }
  r = db_.exec("CREATE INDEX IF NOT EXISTS idx_transfer_resume_state ON transfer_resume(state)");
  if (r) { PQclear(r); }
}

bool ResumeStore::save_resume_state(
    uint64_t transfer_id,
    long long sender_user_id,
    long long receiver_user_id,
    const std::string& filename,
    uint64_t file_size,
    uint32_t chunk_size,
    uint32_t last_acked_chunk_index,
    uint64_t bytes_received,
    const std::string& state,
    const std::string& temp_file_path) {
  
  try {
    // Use UPSERT (INSERT ... ON CONFLICT UPDATE)
    const std::string sql = R"(
      INSERT INTO transfer_resume (
        transfer_id, sender_user_id, receiver_user_id,
        filename, file_size, chunk_size,
        last_acked_chunk_index, bytes_received,
        state, temp_file_path, updated_at
      ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, now())
      ON CONFLICT (transfer_id) DO UPDATE SET
        last_acked_chunk_index = EXCLUDED.last_acked_chunk_index,
        bytes_received = EXCLUDED.bytes_received,
        state = EXCLUDED.state,
        temp_file_path = EXCLUDED.temp_file_path,
        updated_at = now()
    )";
    
    std::vector<std::string> params = {
      std::to_string(transfer_id),
      std::to_string(sender_user_id),
      std::to_string(receiver_user_id),
      filename,
      std::to_string(file_size),
      std::to_string(chunk_size),
      std::to_string(last_acked_chunk_index),
      std::to_string(bytes_received),
      state,
      temp_file_path
    };
    
    PGresult* r = db_.exec_params(sql, params);
    fsx::db::Db::must_ok(r, "save_resume_state");
    PQclear(r);
    return true;
  } catch (const std::exception& e) {
    // Log error but don't fail transfer
    return false;
  }
}

std::optional<ResumeState> ResumeStore::get_resume_state(uint64_t transfer_id) {
  try {
    const std::string sql = R"(
      SELECT transfer_id, sender_user_id, receiver_user_id,
             filename, file_size, chunk_size,
             last_acked_chunk_index, bytes_received,
             state, temp_file_path
      FROM transfer_resume
      WHERE transfer_id = $1 AND state IN ('ACCEPTED', 'RECEIVING')
      LIMIT 1
    )";
    
    PGresult* r = db_.exec_params(sql, {std::to_string(transfer_id)});
    fsx::db::Db::must_ok(r, "get_resume_state");
    
    if (PQntuples(r) == 0) {
      PQclear(r);
      return std::nullopt;
    }
    
    ResumeState state;
    state.transfer_id = std::stoull(PQgetvalue(r, 0, 0));
    state.sender_user_id = std::stoll(PQgetvalue(r, 0, 1));
    state.receiver_user_id = std::stoll(PQgetvalue(r, 0, 2));
    state.filename = PQgetvalue(r, 0, 3) ? PQgetvalue(r, 0, 3) : "";
    state.file_size = std::stoull(PQgetvalue(r, 0, 4));
    state.chunk_size = std::stoul(PQgetvalue(r, 0, 5));
    state.last_acked_chunk_index = std::stoul(PQgetvalue(r, 0, 6));
    state.bytes_received = std::stoull(PQgetvalue(r, 0, 7));
    state.state = PQgetvalue(r, 0, 8) ? PQgetvalue(r, 0, 8) : "";
    state.temp_file_path = PQgetvalue(r, 0, 9) ? PQgetvalue(r, 0, 9) : "";
    
    PQclear(r);
    return state;
  } catch (const std::exception& e) {
    return std::nullopt;
  }
}

bool ResumeStore::clear_resume_state(uint64_t transfer_id) {
  try {
    const std::string sql = "DELETE FROM transfer_resume WHERE transfer_id = $1";
    PGresult* r = db_.exec_params(sql, {std::to_string(transfer_id)});
    fsx::db::Db::must_ok(r, "clear_resume_state");
    PQclear(r);
    return true;
  } catch (const std::exception& e) {
    return false;
  }
}

bool ResumeStore::can_resume(uint64_t transfer_id) {
  auto state = get_resume_state(transfer_id);
  return state.has_value();
}

std::string ResumeStore::state_to_string(const std::string& state) const {
  return state;  // Already a string
}

} // namespace fsx::storage
