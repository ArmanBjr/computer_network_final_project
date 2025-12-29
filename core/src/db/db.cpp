#include "fsx/db/db.h"
#include <stdexcept>
#include <sstream>

namespace fsx::db {

Db::Db(DbConfig cfg) : cfg_(std::move(cfg)) {}

Db::~Db() {
  if (conn_) PQfinish(conn_);
}

std::string Db::conninfo() const {
  std::ostringstream ss;
  ss << "host=" << cfg_.host
     << " port=" << cfg_.port
     << " dbname=" << cfg_.name
     << " user=" << cfg_.user
     << " password=" << cfg_.password
     << " connect_timeout=10";
  return ss.str();
}

void Db::connect() {
  if (conn_) PQfinish(conn_);
  conn_ = PQconnectdb(conninfo().c_str());
  if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
    std::string err = conn_ ? PQerrorMessage(conn_) : "PQconnectdb failed";
    throw std::runtime_error("DB connect failed: " + err);
  }
}

bool Db::is_connected() const {
  return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

PGresult* Db::exec(const std::string& sql) {
  if (!is_connected()) throw std::runtime_error("DB not connected");
  PGresult* r = PQexec(conn_, sql.c_str());
  return r;
}

PGresult* Db::exec_params(const std::string& sql,
                          const std::vector<std::string>& params) {
  if (!is_connected()) throw std::runtime_error("DB not connected");
  std::vector<const char*> values;
  values.reserve(params.size());
  for (auto& p : params) values.push_back(p.c_str());

  PGresult* r = PQexecParams(
      conn_,
      sql.c_str(),
      static_cast<int>(params.size()),
      nullptr,                 // param types (infer)
      values.data(),
      nullptr,                 // lengths
      nullptr,                 // formats
      0                        // result text
  );
  return r;
}

void Db::must_ok(PGresult* r, const std::string& ctx) {
  if (!r) throw std::runtime_error(ctx + ": PGresult is null");
  auto st = PQresultStatus(r);
  if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
    std::string err = PQresultErrorMessage(r);
    PQclear(r);
    throw std::runtime_error(ctx + ": " + err);
  }
}

} // namespace fsx::db

