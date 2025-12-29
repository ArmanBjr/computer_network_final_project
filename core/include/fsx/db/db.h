#pragma once

#include <libpq-fe.h>
#include <string>
#include <vector>
#include <optional>

namespace fsx::db {

struct DbConfig {
  std::string host;
  int port = 5432;
  std::string user;
  std::string password;
  std::string name;
};

class Db {
 public:
  explicit Db(DbConfig cfg);
  ~Db();

  Db(const Db&) = delete;
  Db& operator=(const Db&) = delete;

  void connect();
  bool is_connected() const;

  // Execute a parameterized query: PQexecParams
  PGresult* exec_params(const std::string& sql,
                        const std::vector<std::string>& params);

  // Simple exec (no params)
  PGresult* exec(const std::string& sql);

  static void must_ok(PGresult* r, const std::string& ctx);

 private:
  DbConfig cfg_;
  PGconn* conn_ = nullptr;
  std::string conninfo() const;
};

} // namespace fsx::db

