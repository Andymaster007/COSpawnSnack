#include "core/logger.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace csn {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    path_ = path;
    has_file_ = true;
}

static std::string FormatWithTimestamp(const std::string& level, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    local = *std::localtime(&t);
#endif
    std::stringstream ss;
    ss << "[" << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "][" << level << "] " << msg;
    return ss.str();
}

void Logger::Info(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string line = FormatWithTimestamp("INFO", msg);
    std::cout << line << "\n";
    if (has_file_) {
        std::ofstream f(path_, std::ios::app);
        if (f) f << line << "\n";
    }
}

void Logger::Warn(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string line = FormatWithTimestamp("WARN", msg);
    std::cout << line << "\n";
    if (has_file_) {
        std::ofstream f(path_, std::ios::app);
        if (f) f << line << "\n";
    }
}

void Logger::Error(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string line = FormatWithTimestamp("ERROR", msg);
    std::cerr << line << "\n";
    if (has_file_) {
        std::ofstream f(path_, std::ios::app);
        if (f) f << line << "\n";
    }
}

void Logger::Debug(const std::string& msg) {
    // Debug logging is enabled for now; can be gated by a flag later.
    std::lock_guard<std::mutex> lock(mutex_);
    std::string line = FormatWithTimestamp("DEBUG", msg);
    std::cout << line << "\n";
    if (has_file_) {
        std::ofstream f(path_, std::ios::app);
        if (f) f << line << "\n";
    }
}

} // namespace csn
