#pragma once

#include "fsx/db/db.h"
#include <string>
#include <vector>
#include <optional>

namespace fsx::db {

struct SessionRow {
  long long id;
  long long user_id;
  std::string token;
  std::string expires_at;   // keep as text for now (TIMESTAMPTZ)
  std::string last_seen_at;
};

class SessionRepository {
 public:
  explicit SessionRepository(Db& db) : db_(db) {}

  // creates session and returns token
  std::string create_session(long long user_id, int ttl_seconds);

  std::optional<SessionRow> validate_token(const std::string& token);

  void touch_session(const std::string& token);

  std::vector<SessionRow> list_valid_sessions();

 private:
  Db& db_;
};

} // namespace fsx::db

