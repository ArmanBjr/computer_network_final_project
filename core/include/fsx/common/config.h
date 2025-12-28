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
