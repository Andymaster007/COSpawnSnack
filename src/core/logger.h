#pragma once
#include <string>
#include <mutex>

namespace csn {

class Logger {
public:
    static Logger& Instance();
    void SetFile(const std::string& path);
    void Info(const std::string& msg);
    void Error(const std::string& msg);
    void Debug(const std::string& msg);

private:
    Logger() = default;
    std::mutex mutex_;
    std::string path_;
    bool has_file_ = false;
};

#define CSN_LOG_INFO(msg)  csn::Logger::Instance().Info(msg)
#define CSN_LOG_ERROR(msg) csn::Logger::Instance().Error(msg)
#define CSN_LOG_DEBUG(msg) csn::Logger::Instance().Debug(msg)

} // namespace csn
