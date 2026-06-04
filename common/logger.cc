#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <string>

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

// ── Internal helpers ────────────────────────────────────────────

namespace {

std::string format_timestamp(const char *fmt) {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto secs = time_point_cast<seconds>(now);
    auto us = duration_cast<microseconds>(now - secs).count();

    std::tm lt;
    time_t t = system_clock::to_time_t(secs);
    localtime_r(&t, &lt);

    std::ostringstream oss;
    oss << std::put_time(&lt, fmt)
        << "." << std::setfill('0') << std::setw(6) << us;
    return oss.str();
}

std::string get_file_safe_timestamp() {
    return format_timestamp("%Y-%m-%d_%H-%M-%S");
}

std::string get_process_name() {
    if (program_invocation_short_name &&
        program_invocation_short_name[0]) {
        return program_invocation_short_name;
    }
    return "unknown";
}

}  // anonymous namespace

// ── Public helpers ──────────────────────────────────────────────

std::string get_timestamp() {
    return format_timestamp("%Y-%m-%d %H:%M:%S");
}

std::string get_thread_name() {
    char name[32]{};
    if (pthread_getname_np(pthread_self(), name, sizeof(name)) == 0) {
        return name;
    }
    return "unknown";
}

// ── Filename builders ──────────────────────────────────────────

std::string make_crash_log_filename() {
    auto proc = get_process_name();
    auto thread = get_thread_name();
    auto ts = get_file_safe_timestamp();
    return proc + "+" + thread + "+" + ts + "+coredump.log";
}

std::string make_normal_log_filename() {
    return get_process_name() + ".log";
}

// ── Singleton logger accessors ─────────────────────────────────

Logger &get_crash_logger() {
    static Logger crash_logger("/tmp", make_crash_log_filename(), LogLevel::Trace);
    return crash_logger;
}

Logger &get_default_logger() {
    static Logger default_logger("/tmp", make_normal_log_filename(), LogLevel::Info);
    return default_logger;
}

// ── Custom logger factories ────────────────────────────────────

Logger make_crash_logger(const std::string &log_dir,
                          const std::string &file_name,
                          LogLevel level) {
    return Logger(log_dir, file_name, level);
}

Logger make_default_logger(const std::string &log_dir,
                            const std::string &file_name,
                            LogLevel level) {
    return Logger(log_dir, file_name, level);
}

}  // namespace TOPNSPC
