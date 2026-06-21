#ifndef COMMON_LOGGER_H
#define COMMON_LOGGER_H

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "common_fwd.h"

namespace TOPNSPC {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

namespace logger_internal {
struct DefaultModule {
    constexpr operator const char *() const { return "Global"; }
};
inline constexpr DefaultModule LOG_MODULE_FALLBACK;
}  // namespace logger_internal

class Logger {
public:
    Logger(LogLevel log_level = LogLevel::Info);
    Logger(const std::string &log_dir, const std::string &file_name, LogLevel log_level = LogLevel::Info);
    ~Logger();

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    Logger(Logger &&) noexcept = default;
    Logger &operator=(Logger &&) noexcept = default;

    spdlog::logger *operator->() const;

    bool valid() const { return spd_logger_ != nullptr; }

private:
    static spdlog::level::level_enum to_spdlog_level(LogLevel level);

    std::shared_ptr<spdlog::logger> spd_logger_;
};

}  // namespace TOPNSPC

namespace TOPNSPC::logger_internal {
template <typename T>
constexpr decltype(auto) get_mod_name(T &&val) { return std::forward<T>(val); }
}  // namespace TOPNSPC::logger_internal

namespace TOPNSPC {

inline constexpr logger_internal::DefaultModule LOG_MODULE = logger_internal::LOG_MODULE_FALLBACK;

// --- Helpers ---
std::string get_timestamp();
std::string get_thread_name();

// --- Filename builders ---
std::string make_crash_log_filename();
std::string make_normal_log_filename();

// --- Default logger accessors (singletons) ---
Logger &get_crash_logger();
Logger &get_default_logger();

// --- Custom logger factories ---
Logger make_crash_logger(const std::string &log_dir,
                         const std::string &file_name,
                         LogLevel level = LogLevel::Trace);
Logger make_default_logger(const std::string &log_dir,
                           const std::string &file_name,
                           LogLevel level = LogLevel::Info);

}  // namespace TOPNSPC

template <>
struct fmt::formatter<TOPNSPC::logger_internal::DefaultModule> : fmt::formatter<std::string_view> {
    auto format(const TOPNSPC::logger_internal::DefaultModule &mod, fmt::format_context &ctx) const {
        return fmt::formatter<std::string_view>::format(static_cast<const char *>(mod), ctx);
    }
};

#define MOD_PRINT_ACTIVE(logger, level, fmt, ...) \
    (logger)->level("[{}] " fmt, ::TOPNSPC::logger_internal::get_mod_name(::TOPNSPC::LOG_MODULE), ##__VA_ARGS__)

#define MOD_TRACE(logger, fmt, ...) MOD_PRINT_ACTIVE(logger, trace, fmt, ##__VA_ARGS__)
#define MOD_DEBUG(logger, fmt, ...) MOD_PRINT_ACTIVE(logger, debug, fmt, ##__VA_ARGS__)
#define MOD_INFO(logger, fmt, ...) MOD_PRINT_ACTIVE(logger, info, fmt, ##__VA_ARGS__)
#define MOD_WARN(logger, fmt, ...) MOD_PRINT_ACTIVE(logger, warn, fmt, ##__VA_ARGS__)
#define MOD_ERROR(logger, fmt, ...) MOD_PRINT_ACTIVE(logger, error, fmt, ##__VA_ARGS__)

#endif  // COMMON_LOGGER_H
