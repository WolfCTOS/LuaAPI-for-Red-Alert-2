#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
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

    // Must be called once from a worker thread (never inside DllMain).
    // Idempotent; silently disables logging if the file cannot be opened.
    void Init(const std::wstring& logPath) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (logger_) return;
        try {
            int size = WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string narrowPath(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, logPath.c_str(), -1, &narrowPath[0], size, nullptr, nullptr);
            narrowPath.resize(size - 1);
            logger_ = spdlog::rotating_logger_mt("luaapi", narrowPath, 5 * 1024 * 1024, 3);
            logger_->set_pattern("[%T.%e] [%l] %v");
            logger_->set_level(spdlog::level::trace);
            logger_->flush_on(spdlog::level::info);
        } catch (...) {
            logger_ = nullptr;
        }
    }

    bool ready() const { return logger_ != nullptr; }

    template <typename... Args>
    void log(spdlog::level::level_enum lvl, const char* file, int line, spdlog::string_view_t fmt, Args&&... args) {
        if (!logger_) return;
        try {
            std::string msg = spdlog::fmt_lib::format(fmt, std::forward<Args>(args)...);
            std::lock_guard<std::mutex> lock(mutex_);
            logger_->log(lvl, "{} ({}:{})", msg, file, line);
        } catch (...) {
        }
    }

    // Принудительный сброс буфера лога на диск. Критично для диагностики крашей:
    // при аварийном завершении процесса невыгруженный буфер spdlog теряется.
    void FlushLog() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (logger_) {
            // __try не может находиться в функции с локальными объектами (C2712),
            // поэтому реальный flush вынесен в отдельную статическую функцию.
            FlushImpl(logger_.get());
        }
    }

private:
    // Не может иметь локальные объекты с деструктором (иначе C2712 при __try).
    static void FlushImpl(spdlog::logger* pLogger) {
        if (!pLogger) return;
        __try { pLogger->flush(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { }
    }

    Logger() = default;

    std::shared_ptr<spdlog::logger> logger_;
    std::mutex mutex_;
};

} // namespace LuaAPI

#define LUA_LOG_TRACE(...) ::LuaAPI::Logger::instance().log(spdlog::level::trace, __FILE__, __LINE__, __VA_ARGS__)
#define LUA_LOG_INFO(...)  ::LuaAPI::Logger::instance().log(spdlog::level::info,  __FILE__, __LINE__, __VA_ARGS__)
#define LUA_LOG_WARN(...)  ::LuaAPI::Logger::instance().log(spdlog::level::warn,  __FILE__, __LINE__, __VA_ARGS__)
#define LUA_LOG_ERROR(...) ::LuaAPI::Logger::instance().log(spdlog::level::err,   __FILE__, __LINE__, __VA_ARGS__)

// LUA_LOG_CRITICAL: пишет с уровнем critical и НЕМЕДЛЕННО сбрасывает буфер на диск.
// Это гарантирует, что последнее сообщение перед крашем (Access Violation / нет)
// попадает в лог-файл, а не теряется в невыгруженном буфере spdlog.
#define LUA_LOG_CRITICAL(...)              \
    do {                                    \
        ::LuaAPI::Logger::instance().log(spdlog::level::critical, __FILE__, __LINE__, __VA_ARGS__); \
        ::LuaAPI::Logger::instance().FlushLog(); \
    } while (0)

// LUA_FLUSH_LOG: принудительный сброс буфера лога на диск (без записи сообщения).
// Используется перед потенциально опасной операцией, чтобы последние логи не
// потерялись, если операция упадёт (краш/SpawnManager и т.п.).
#define LUA_FLUSH_LOG() ::LuaAPI::Logger::instance().FlushLog()
