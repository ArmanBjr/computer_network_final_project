#include "fsx/log/logger.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace fsx {

static std::string now_iso() {
    using namespace std::chrono;
    auto tp = system_clock::now();
    auto t = system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    path_ = path;
    std::ofstream f(path_, std::ios::app);
    f << now_iso() << " [INFO] logger initialized\n";
}

void Logger::write(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    std::ofstream f(path_, std::ios::app);
    f << now_iso() << " [" << level << "] " << msg << "\n";
}

void Logger::info(const std::string& msg) { write("INFO", msg); }
void Logger::warn(const std::string& msg) { write("WARN", msg); }
void Logger::error(const std::string& msg) { write("ERROR", msg); }

} // namespace fsx
