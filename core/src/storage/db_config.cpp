#include "fsx/storage/db_config.h"
#include <cstdlib>

namespace fsx {

static std::string getenv_or(const char* key, const char* defv) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : std::string(defv);
}

DbConfig load_db_config_from_env() {
  DbConfig cfg;
  cfg.host = getenv_or("FSX_DB_HOST", "127.0.0.1");
  cfg.port = getenv_or("FSX_DB_PORT", "5432");
  cfg.user = getenv_or("FSX_DB_USER", "fsx");
  cfg.password = getenv_or("FSX_DB_PASSWORD", "fsxpass");
  cfg.dbname = getenv_or("FSX_DB_NAME", "fsx");
  return cfg;
}

} // namespace fsx
