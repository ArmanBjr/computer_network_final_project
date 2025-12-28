#pragma once
#include <libpq-fe.h>
#include <stdexcept>
#include <string>

#include "fsx/storage/db_config.h"

namespace fsx {

class DbError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class DbClient {
public:
  DbClient() = default;
  ~DbClient();

  DbClient(const DbClient&) = delete;
  DbClient& operator=(const DbClient&) = delete;

  void connect(const DbConfig& cfg);
  bool connected() const;

  // Simple exec without params - enough for Phase2
  PGresult* exec(const std::string& sql);

  static void expect(PGresult* r, ExecStatusType expected, const char* what);

private:
  PGconn* conn_ = nullptr;
};

} // namespace fsx
