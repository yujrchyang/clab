#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "common/logger.h"

TEST(LoggerTest, DefaultConstruction) {
    TOPNSPC::Logger logger;
    EXPECT_NE(logger.operator->(), nullptr);
}

TEST(LoggerTest, FileLoggerConstruction) {
    std::string log_dir = "./test_logs";
    std::string file_name = "test.log";
    TOPNSPC::Logger logger(log_dir, file_name, TOPNSPC::LogLevel::Trace);
    EXPECT_NE(logger.operator->(), nullptr);
    std::filesystem::path log_path(log_dir);
    EXPECT_TRUE(std::filesystem::exists(log_path));
}

TEST(LoggerTest, LogAllLevels) {
    TOPNSPC::Logger logger("./test_logs", "levels.log", TOPNSPC::LogLevel::Trace);
    EXPECT_NO_THROW(logger->trace("trace message"));
    EXPECT_NO_THROW(logger->debug("debug message"));
    EXPECT_NO_THROW(logger->info("info message"));
    EXPECT_NO_THROW(logger->warn("warn message"));
    EXPECT_NO_THROW(logger->error("error message"));
}

TEST(LoggerTest, ModMacros) {
    TOPNSPC::Logger logger("./test_logs", "macros.log", TOPNSPC::LogLevel::Trace);
    EXPECT_NO_THROW(MOD_TRACE(logger, "MOD_TRACE: {}", 1));
    EXPECT_NO_THROW(MOD_DEBUG(logger, "MOD_DEBUG: {}", 2));
    EXPECT_NO_THROW(MOD_INFO(logger, "MOD_INFO: {}", 3));
    EXPECT_NO_THROW(MOD_WARN(logger, "MOD_WARN: {}", 4));
    EXPECT_NO_THROW(MOD_ERROR(logger, "MOD_ERROR: {}", 5));
}

TEST(LoggerTest, MoveSemantics) {
    TOPNSPC::Logger logger1("./test_logs", "move.log", TOPNSPC::LogLevel::Info);
    TOPNSPC::Logger logger2 = std::move(logger1);
    EXPECT_NE(logger2.operator->(), nullptr);
    EXPECT_NO_THROW(logger2->info("after move, info"));
}

TEST(LoggerTest, LevelFiltering) {
    TOPNSPC::Logger logger("./test_logs", "filter.log", TOPNSPC::LogLevel::Error);
    EXPECT_NO_THROW(logger->error("error only: visible"));
    EXPECT_NO_THROW(logger->info("info: should be suppressed"));
    EXPECT_NO_THROW(logger->trace("trace: should be suppressed"));
}

TEST(LoggerTest, MultipleLoggers) {
    TOPNSPC::Logger logger1(TOPNSPC::LogLevel::Info);
    TOPNSPC::Logger logger2("./test_logs", "multi.log", TOPNSPC::LogLevel::Debug);
    EXPECT_NO_THROW(logger1->info("logger1: console only"));
    EXPECT_NO_THROW(logger2->debug("logger2: file output"));
}

TEST(LoggerTest, EmptyDirDefaultsToConsole) {
    TOPNSPC::Logger logger("", "", TOPNSPC::LogLevel::Warn);
    EXPECT_NE(logger.operator->(), nullptr);
    EXPECT_NO_THROW(logger->warn("console-only warn"));
}
