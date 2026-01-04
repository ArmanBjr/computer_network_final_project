#pragma once

#include "fsx/db/db.h"
#include <optional>
#include <string>

namespace fsx::db {

struct UserRow {
  long long id;
  std::string username;
  std::string email;
  std::string pass_hash;
};

class UserRepository {
 public:
  explicit UserRepository(Db& db) : db_(db) {}

  // returns created user_id
  long long create_user(const std::string& username, const std::string& email, const std::string& pass_hash);

  std::optional<UserRow> get_user_by_username(const std::string& username);

 private:
  Db& db_;
};

} // namespace fsx::db

