#pragma once
#include <mutex>
#include <string>

namespace fsx {

class Logger {
public:
    static Logger& instance();

    void init(const std::string& path);
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);

private:
    Logger() = default;
    void write(const std::string& level, const std::string& msg);

    std::mutex mu_;
    std::string path_;
};

} // namespace fsx
