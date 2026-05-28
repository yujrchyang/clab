#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "common/safe_io.h"

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        char tmpl[] = "/tmp/safe_io_test_XXXXXX";
        if (mkdtemp(tmpl))
            path = tmpl;
    }
    ~TempDir() {
        if (!path.empty())
            std::filesystem::remove_all(path);
    }
};

struct TempFile {
    std::string path;
    int fd = -1;

    TempFile() {
        char tmpl[] = "/tmp/safe_io_file_XXXXXX";
        fd = mkstemp(tmpl);
        if (fd >= 0)
            path = tmpl;
    }
    ~TempFile() {
        if (fd >= 0)
            close(fd);
        if (!path.empty())
            unlink(path.c_str());
    }
};

TEST(SafeIOTest, ReadWriteRoundTrip) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "hello safe_io";
    size_t len = sizeof(data);

    ssize_t ret = safe_write(f.fd, data, len);
    ASSERT_EQ(ret, 0);

    lseek(f.fd, 0, SEEK_SET);

    char buf[64] = {};
    ret = safe_read(f.fd, buf, len);
    ASSERT_EQ(ret, static_cast<ssize_t>(len));
    ASSERT_EQ(std::memcmp(buf, data, len), 0);
}

TEST(SafeIOTest, ReadWriteExact) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "exact match";
    size_t len = sizeof(data);

    ssize_t ret = safe_write(f.fd, data, len);
    ASSERT_EQ(ret, 0);

    lseek(f.fd, 0, SEEK_SET);

    char buf[64] = {};
    ret = safe_read_exact(f.fd, buf, len);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(std::memcmp(buf, data, len), 0);
}

TEST(SafeIOTest, ShortReadReturnsError) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "short";
    size_t written = sizeof(data);
    ssize_t ret = safe_write(f.fd, data, written);
    ASSERT_EQ(ret, 0);

    lseek(f.fd, 0, SEEK_SET);

    char buf[64] = {};
    ret = safe_read_exact(f.fd, buf, written + 10);
    ASSERT_EQ(ret, -EDOM);
}

TEST(SafeIOTest, SendIsAliasForWrite) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "send test";
    size_t len = sizeof(data);

    ssize_t ret = safe_send(f.fd, data, len);
    ASSERT_EQ(ret, 0);

    lseek(f.fd, 0, SEEK_SET);

    char buf[64] = {};
    ret = safe_read(f.fd, buf, len);
    ASSERT_EQ(ret, static_cast<ssize_t>(len));
    ASSERT_EQ(std::memcmp(buf, data, len), 0);
}

TEST(SafeIOTest, RecvIsAliasForRead) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "recv test";
    size_t len = sizeof(data);

    ssize_t ret = safe_write(f.fd, data, len);
    ASSERT_EQ(ret, 0);

    lseek(f.fd, 0, SEEK_SET);

    char buf[64] = {};
    ret = safe_recv(f.fd, buf, len);
    ASSERT_EQ(ret, static_cast<ssize_t>(len));
    ASSERT_EQ(std::memcmp(buf, data, len), 0);
}

TEST(SafeIOTest, PreadPwriteRoundTrip) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "positional io";
    size_t len = sizeof(data);
    off_t offset = 0;

    ssize_t ret = safe_pwrite(f.fd, data, len, offset);
    ASSERT_EQ(ret, 0);

    char buf[64] = {};
    ret = safe_pread(f.fd, buf, len, offset);
    ASSERT_EQ(ret, static_cast<ssize_t>(len));
    ASSERT_EQ(std::memcmp(buf, data, len), 0);
}

TEST(SafeIOTest, PreadExact) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "pread_exact data";
    size_t len = sizeof(data);

    ssize_t ret = safe_pwrite(f.fd, data, len, 0);
    ASSERT_EQ(ret, 0);

    char buf[64] = {};
    ret = safe_pread_exact(f.fd, buf, len, 0);
    ASSERT_EQ(ret, 0);
    ASSERT_EQ(std::memcmp(buf, data, len), 0);
}

TEST(SafeIOTest, PreadExactShort) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "short pread";
    size_t len = sizeof(data);

    ssize_t ret = safe_pwrite(f.fd, data, len, 0);
    ASSERT_EQ(ret, 0);

    char buf[64] = {};
    ret = safe_pread_exact(f.fd, buf, len + 10, 0);
    ASSERT_EQ(ret, -EDOM);
}

TEST(SafeIOTest, WriteFileAndReadFile) {
    TempDir dir;
    ASSERT_FALSE(dir.path.empty());

    const char content[] = "file content";
    size_t len = sizeof(content);

    int ret = safe_write_file(dir.path.c_str(), "test.txt", content, len,
                              0644);
    ASSERT_EQ(ret, 0);

    char buf[64] = {};
    ret = safe_read_file(dir.path.c_str(), "test.txt", buf, sizeof(buf));
    ASSERT_EQ(ret, static_cast<int>(len));
    ASSERT_EQ(std::memcmp(buf, content, len), 0);
}

TEST(SafeIOTest, WriteFileIdempotentSameContent) {
    TempDir dir;
    ASSERT_FALSE(dir.path.empty());

    const char content[] = "same content";
    size_t len = sizeof(content);

    int ret = safe_write_file(dir.path.c_str(), "same.txt", content, len,
                              0644);
    ASSERT_EQ(ret, 0);

    ret = safe_write_file(dir.path.c_str(), "same.txt", content, len, 0644);
    ASSERT_EQ(ret, 0);
}

TEST(SafeIOTest, ReadFileNotFound) {
    TempDir dir;
    ASSERT_FALSE(dir.path.empty());

    char buf[64] = {};
    int ret = safe_read_file(dir.path.c_str(), "nonexistent", buf,
                             sizeof(buf));
    ASSERT_LT(ret, 0);
}

TEST(SafeIOTest, EmptyWriteAndRead) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    ssize_t ret = safe_write(f.fd, "", 0);
    ASSERT_EQ(ret, 0);

    char buf[1] = {};
    ret = safe_read(f.fd, buf, 0);
    ASSERT_EQ(ret, 0);
}

TEST(SafeIOTest, InvalidFdReturnsError) {
    char buf[16] = {};
    ssize_t ret = safe_read(-1, buf, sizeof(buf));
    ASSERT_LT(ret, 0);

    ret = safe_write(-1, "data", 4);
    ASSERT_LT(ret, 0);

    ret = safe_pread(-1, buf, sizeof(buf), 0);
    ASSERT_LT(ret, 0);

    ret = safe_pwrite(-1, "data", 4, 0);
    ASSERT_LT(ret, 0);
}

TEST(SafeIOTest, LargeData) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    std::vector<char> data(65536);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<char>(i);

    ssize_t ret = safe_write(f.fd, data.data(), data.size());
    ASSERT_EQ(ret, 0);

    lseek(f.fd, 0, SEEK_SET);

    std::vector<char> buf(data.size());
    ret = safe_read(f.fd, buf.data(), buf.size());
    ASSERT_EQ(ret, static_cast<ssize_t>(buf.size()));
    ASSERT_EQ(std::memcmp(buf.data(), data.data(), data.size()), 0);
}

TEST(SafeIOTest, PartialPreadFromOffset) {
    TempFile f;
    ASSERT_GE(f.fd, 0);

    const char data[] = "0123456789";
    size_t len = sizeof(data) - 1;

    ssize_t ret = safe_pwrite(f.fd, data, len, 0);
    ASSERT_EQ(ret, 0);

    char buf[16] = {};
    ret = safe_pread(f.fd, buf, 5, 2);
    ASSERT_EQ(ret, 5);
    ASSERT_EQ(std::memcmp(buf, "23456", 5), 0);
}
