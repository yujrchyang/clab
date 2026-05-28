#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "common/logger.hpp"

TEST(LoggerTest, DefaultConstruction) {
    clab::Logger logger;
    EXPECT_NE(logger.operator->(), nullptr);
}

TEST(LoggerTest, FileLoggerConstruction) {
    std::string log_dir = "./test_logs";
    std::string file_name = "test.log";
    clab::Logger logger(log_dir, file_name, clab::LogLevel::Trace);
    EXPECT_NE(logger.operator->(), nullptr);
    std::filesystem::path log_path(log_dir);
    EXPECT_TRUE(std::filesystem::exists(log_path));
}

TEST(LoggerTest, LogAllLevels) {
    clab::Logger logger("./test_logs", "levels.log", clab::LogLevel::Trace);
    EXPECT_NO_THROW(logger->trace("trace message"));
    EXPECT_NO_THROW(logger->debug("debug message"));
    EXPECT_NO_THROW(logger->info("info message"));
    EXPECT_NO_THROW(logger->warn("warn message"));
    EXPECT_NO_THROW(logger->error("error message"));
}

TEST(LoggerTest, ModMacros) {
    clab::Logger logger("./test_logs", "macros.log", clab::LogLevel::Trace);
    EXPECT_NO_THROW(MOD_TRACE(logger, "MOD_TRACE: {}", 1));
    EXPECT_NO_THROW(MOD_DEBUG(logger, "MOD_DEBUG: {}", 2));
    EXPECT_NO_THROW(MOD_INFO(logger, "MOD_INFO: {}", 3));
    EXPECT_NO_THROW(MOD_WARN(logger, "MOD_WARN: {}", 4));
    EXPECT_NO_THROW(MOD_ERROR(logger, "MOD_ERROR: {}", 5));
}

TEST(LoggerTest, MoveSemantics) {
    clab::Logger logger1("./test_logs", "move.log", clab::LogLevel::Info);
    clab::Logger logger2 = std::move(logger1);
    EXPECT_NE(logger2.operator->(), nullptr);
    EXPECT_NO_THROW(logger2->info("after move, info"));
}

TEST(LoggerTest, LevelFiltering) {
    clab::Logger logger("./test_logs", "filter.log", clab::LogLevel::Error);
    EXPECT_NO_THROW(logger->error("error only: visible"));
    EXPECT_NO_THROW(logger->info("info: should be suppressed"));
    EXPECT_NO_THROW(logger->trace("trace: should be suppressed"));
}

TEST(LoggerTest, MultipleLoggers) {
    clab::Logger logger1(clab::LogLevel::Info);
    clab::Logger logger2("./test_logs", "multi.log", clab::LogLevel::Debug);
    EXPECT_NO_THROW(logger1->info("logger1: console only"));
    EXPECT_NO_THROW(logger2->debug("logger2: file output"));
}

TEST(LoggerTest, EmptyDirDefaultsToConsole) {
    clab::Logger logger("", "", clab::LogLevel::Warn);
    EXPECT_NE(logger.operator->(), nullptr);
    EXPECT_NO_THROW(logger->warn("console-only warn"));
}
