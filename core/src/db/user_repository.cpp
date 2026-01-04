#include "fsx/db/user_repository.h"
#include <libpq-fe.h>
#include <stdexcept>

namespace fsx::db {

long long UserRepository::create_user(const std::string& username,
                                      const std::string& email,
                                      const std::string& pass_hash) {
  // INSERT ... RETURNING id
  const std::string sql =
      "INSERT INTO users(username, email, pass_hash) VALUES ($1, $2, $3) RETURNING id;";
  PGresult* r = db_.exec_params(sql, {username, email, pass_hash});
  Db::must_ok(r, "create_user");

  if (PQntuples(r) != 1) {
    PQclear(r);
    throw std::runtime_error("create_user: unexpected row count");
  }

  char* id_str = PQgetvalue(r, 0, 0);
  long long id = std::stoll(id_str);
  PQclear(r);
  return id;
}

std::optional<UserRow> UserRepository::get_user_by_username(const std::string& username) {
  const std::string sql =
      "SELECT id, username, email, pass_hash FROM users WHERE username = $1 LIMIT 1;";
  PGresult* r = db_.exec_params(sql, {username});
  Db::must_ok(r, "get_user_by_username");

  if (PQntuples(r) == 0) {
    PQclear(r);
    return std::nullopt;
  }

  UserRow u;
  u.id = std::stoll(PQgetvalue(r, 0, 0));
  u.username = PQgetvalue(r, 0, 1);
  u.email = PQgetvalue(r, 0, 2) ? PQgetvalue(r, 0, 2) : "";
  u.pass_hash = PQgetvalue(r, 0, 3);
  PQclear(r);
  return u;
}

} // namespace fsx::db

