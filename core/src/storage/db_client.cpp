#include "fsx/storage/db_client.h"
#include <sstream>

namespace fsx {

DbClient::~DbClient() {
  if (conn_) {
    PQfinish(conn_);
    conn_ = nullptr;
  }
}

void DbClient::connect(const DbConfig& cfg) {
  if (conn_) {
    PQfinish(conn_);
    conn_ = nullptr;
  }

  std::ostringstream ss;
  ss << "host=" << cfg.host
     << " port=" << cfg.port
     << " user=" << cfg.user
     << " password=" << cfg.password
     << " dbname=" << cfg.dbname;

  conn_ = PQconnectdb(ss.str().c_str());
  if (!conn_) {
    throw DbError("PQconnectdb returned null");
  }
  if (PQstatus(conn_) != CONNECTION_OK) {
    std::string err = PQerrorMessage(conn_);
    PQfinish(conn_);
    conn_ = nullptr;
    throw DbError("DB connect failed: " + err);
  }
}

bool DbClient::connected() const {
  return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

PGresult* DbClient::exec(const std::string& sql) {
  if (!connected()) {
    throw DbError("DB not connected");
  }
  PGresult* r = PQexec(conn_, sql.c_str());
  if (!r) {
    throw DbError("PQexec returned null");
  }
  return r;
}

void DbClient::expect(PGresult* r, ExecStatusType expected, const char* what) {
  if (!r) throw DbError(std::string(what) + ": null PGresult");
  ExecStatusType st = PQresultStatus(r);
  if (st != expected) {
    std::string err = PQresultErrorMessage(r);
    PQclear(r);
    throw DbError(std::string(what) + " failed: " + err);
  }
}

} // namespace fsx
