#include "fsx/db/session_repository.h"
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <libpq-fe.h>

namespace fsx::db {

static std::string random_hex_token(size_t nbytes) {
  std::vector<unsigned char> buf(nbytes);
  if (RAND_bytes(buf.data(), (int)buf.size()) != 1) {
    throw std::runtime_error("RAND_bytes failed");
  }
  std::ostringstream ss;
  for (unsigned char b : buf) ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
  return ss.str();
}

std::string SessionRepository::create_session(long long user_id, int ttl_seconds) {
  // token: 32 bytes => 64 hex chars
  std::string token = random_hex_token(32);

  // expires_at = now() + interval
  const std::string sql =
      "INSERT INTO sessions(user_id, token, expires_at) "
      "VALUES ($1, $2, now() + ($3 || ' seconds')::interval) "
      "RETURNING token;";
  PGresult* r = db_.exec_params(sql, {std::to_string(user_id), token, std::to_string(ttl_seconds)});
  Db::must_ok(r, "create_session");

  if (PQntuples(r) != 1) {
    PQclear(r);
    throw std::runtime_error("create_session: unexpected row count");
  }

  std::string out = PQgetvalue(r, 0, 0);
  PQclear(r);
  return out;
}

std::optional<SessionRow> SessionRepository::validate_token(const std::string& token) {
  const std::string sql =
      "SELECT id, user_id, token, expires_at::text, last_seen_at::text "
      "FROM sessions "
      "WHERE token = $1 AND expires_at > now() "
      "LIMIT 1;";
  PGresult* r = db_.exec_params(sql, {token});
  Db::must_ok(r, "validate_token");

  if (PQntuples(r) == 0) {
    PQclear(r);
    return std::nullopt;
  }

  SessionRow s;
  s.id = std::stoll(PQgetvalue(r, 0, 0));
  s.user_id = std::stoll(PQgetvalue(r, 0, 1));
  s.token = PQgetvalue(r, 0, 2);
  s.expires_at = PQgetvalue(r, 0, 3);
  s.last_seen_at = PQgetvalue(r, 0, 4);
  PQclear(r);
  return s;
}

void SessionRepository::touch_session(const std::string& token) {
  const std::string sql =
      "UPDATE sessions SET last_seen_at = now() WHERE token = $1;";
  PGresult* r = db_.exec_params(sql, {token});
  Db::must_ok(r, "touch_session");
  PQclear(r);
}

std::vector<SessionRow> SessionRepository::list_valid_sessions() {
  const std::string sql =
      "SELECT id, user_id, token, expires_at::text, last_seen_at::text "
      "FROM sessions WHERE expires_at > now() "
      "ORDER BY last_seen_at DESC;";
  PGresult* r = db_.exec(sql);
  Db::must_ok(r, "list_valid_sessions");

  std::vector<SessionRow> out;
  int n = PQntuples(r);
  out.reserve(n);

  for (int i = 0; i < n; i++) {
    SessionRow s;
    s.id = std::stoll(PQgetvalue(r, i, 0));
    s.user_id = std::stoll(PQgetvalue(r, i, 1));
    s.token = PQgetvalue(r, i, 2);
    s.expires_at = PQgetvalue(r, i, 3);
    s.last_seen_at = PQgetvalue(r, i, 4);
    out.push_back(std::move(s));
  }

  PQclear(r);
  return out;
}

} // namespace fsx::db

