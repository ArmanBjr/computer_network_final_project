#pragma once
#include <cstdlib>
#include <string>

namespace fsx {

inline std::string getenv_or(const char* k, const std::string& def) {
    const char* v = std::getenv(k);
    return (v && *v) ? std::string(v) : def;
}

struct Config {
    int tcp_port = 9000;
    int admin_port = 9100;
    std::string log_path = "./report.log";

    static Config from_env() {
        Config c;
        c.tcp_port = std::stoi(getenv_or("FSX_TCP_PORT", "9000"));
        c.admin_port = std::stoi(getenv_or("FSX_ADMIN_PORT", "9100"));
        c.log_path = getenv_or("FSX_LOG_PATH", "./report.log");
        return c;
    }
};

} // namespace fsx

static std::string getenv_or(const char* k, const char* defv) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string(defv);
  }
  
  fsx::DbConfig load_db_cfg() {
    fsx::DbConfig cfg;
    cfg.host = getenv_or("FSX_DB_HOST", "127.0.0.1");
    cfg.port = getenv_or("FSX_DB_PORT", "5432");
    cfg.user = getenv_or("FSX_DB_USER", "fsx");
    cfg.password = getenv_or("FSX_DB_PASSWORD", "fsxpass");
    cfg.dbname = getenv_or("FSX_DB_NAME", "fsx");
    return cfg;
  }
  