#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <memory>
#include <mutex>
#include <string>

namespace LuaAPI {

class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    template <typename... Args>
    void log(spdlog::level::level_enum lvl, const char* file, int line, spdlog::string_view_t fmt, Args&&... args) {
        if (!logger_) return;
        std::string msg = spdlog::fmt_lib::format(fmt, std::forward<Args>(args)...);
        logger_->log(lvl, "{} ({}:{})", msg, file, line);
    }

    void flush() { if (logger_) logger_->flush(); }

private:
    Logger() {
        try {
            logger_ = spdlog::basic_file_logger_mt("luaapi", "D:\\Games\\Red Alert 2\\LuaAPI.log");
            logger_->set_pattern("[%T.%e] [%l] %v");
            logger_->set_level(spdlog::level::trace);
            logger_->flush_on(spdlog::level::info);
        } catch (...) {
            logger_ = nullptr;
        }
    }

    std::shared_ptr<spdlog::logger> logger_;
};

} // namespace LuaAPI

#define LUA_LOG_TRACE(...) ::LuaAPI::Logger::instance().log(spdlog::level::trace, __FILE__, __LINE__, __VA_ARGS__)
#define LUA_LOG_INFO(...)  ::LuaAPI::Logger::instance().log(spdlog::level::info,  __FILE__, __LINE__, __VA_ARGS__)
#define LUA_LOG_WARN(...)  ::LuaAPI::Logger::instance().log(spdlog::level::warn,  __FILE__, __LINE__, __VA_ARGS__)
#define LUA_LOG_ERROR(...) ::LuaAPI::Logger::instance().log(spdlog::level::err,   __FILE__, __LINE__, __VA_ARGS__)
