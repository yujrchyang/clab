#include "logger.h"

namespace TOPNSPC {

Logger::Logger(LogLevel log_level)
    : Logger("", "", log_level) {}

Logger::Logger(const std::string &log_dir, const std::string &file_name, LogLevel log_level) {
    try {
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        if (!log_dir.empty() && !file_name.empty()) {
            std::filesystem::path dir_path(log_dir);
            if (!std::filesystem::exists(dir_path)) {
                std::filesystem::create_directories(dir_path);
            }
            std::filesystem::path log_file_path = dir_path / file_name;
            auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_file_path.string(), 1024 * 1024 * 5, 3);
            spd_logger_ = std::make_shared<spdlog::logger>(
                file_name, spdlog::sinks_init_list{console_sink, file_sink});
        } else {
            spd_logger_ = std::make_shared<spdlog::logger>(
                "console_logger", spdlog::sinks_init_list{console_sink});
        }

        spd_logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spd_logger_->set_level(to_spdlog_level(log_level));
        spd_logger_->flush_on(spdlog::level::warn);
    } catch (const std::exception &ex) {
        fprintf(stderr, "failed to init logger: %s\n", ex.what());
    }
}

Logger::~Logger() {
    if (spd_logger_) spd_logger_->flush();
}

spdlog::logger *Logger::operator->() const {
    return spd_logger_.get();
}

spdlog::level::level_enum Logger::to_spdlog_level(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warn:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    default:
        return spdlog::level::info;
    }
}

}  // namespace TOPNSPC
