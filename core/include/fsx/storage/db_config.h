#pragma once
#include <string>

namespace fsx {

struct DbConfig {
  std::string host;
  std::string port;
  std::string user;
  std::string password;
  std::string dbname;
};

DbConfig load_db_config_from_env();

} // namespace fsx
