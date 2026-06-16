#include <gtest/gtest.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

#include "blk/block_device.h"
#include "blk/io_context.h"
#include "blk/kernel_device.h"
#include "clab_test.h"

using TOPNSPC::bufferlist;

// Temp-file fixture: creates a 4 MiB temp file, opens with KernelDevice,
// destroys on teardown.
class KernelDeviceTest : public ::testing::Test {
protected:
    std::string tmp_path_;
    int tmp_fd_ = -1;
    static constexpr uint64_t kFileSize = 4 << 20;  // 4 MiB

    void SetUp() override {
        auto tmpl = clab_tmp_path("kernel_device");
        tmp_fd_ = ::mkstemp(tmpl.data());
        ASSERT_GE(tmp_fd_, 0) << "mkstemp failed";
        tmp_path_ = tmpl;

        // Allocate file
        int r = ::fallocate(tmp_fd_, 0, 0, kFileSize);
        if (r < 0) {
            // fallback: write zeroes
            std::vector<char> zeros(4096, 0);
            for (uint64_t off = 0; off < kFileSize; off += zeros.size()) {
                ::pwrite(tmp_fd_, zeros.data(),
                         std::min<uint64_t>(zeros.size(), kFileSize - off), off);
            }
        }
    }

    void TearDown() override {
        if (tmp_fd_ >= 0) {
            ::close(tmp_fd_);
            tmp_fd_ = -1;
        }
        if (!tmp_path_.empty()) {
            ::unlink(tmp_path_.c_str());
            tmp_path_.clear();
        }
    }
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, FactoryCreatesKernelDevice) {
    auto dev = BlockDevice::create(tmp_path_, nullptr, nullptr);
    ASSERT_NE(dev, nullptr);
    // Factory creates without opening; caller must open
}

// ---------------------------------------------------------------------------
// Open / close
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, OpenNonExistentFails) {
    KernelDevice dev("/no/such/device", nullptr, nullptr);
    int r = dev.open("/no/such/device");
    EXPECT_LT(r, 0);
}

TEST_F(KernelDeviceTest, OpenCloseRoundtrip) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    int r = dev.open(tmp_path_);
    ASSERT_EQ(r, 0);
    EXPECT_EQ(dev.get_size(), kFileSize);
    EXPECT_GT(dev.get_block_size(), 0);

    dev.close();
    // Should be reusable
    r = dev.open(tmp_path_);
    ASSERT_EQ(r, 0);
    dev.close();
}

TEST_F(KernelDeviceTest, CloseMultipleTimesSafe) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);
    dev.close();
    dev.close();  // second close — no crash
}

// ---------------------------------------------------------------------------
// is_valid_io
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, IsValidIoValid) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);
    EXPECT_TRUE(dev.is_valid_io(0, 4096));
    EXPECT_TRUE(dev.is_valid_io(4096, 4096));
    EXPECT_TRUE(dev.is_valid_io(0, kFileSize));
    dev.close();
}

TEST_F(KernelDeviceTest, IsValidIoInvalid) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);
    EXPECT_FALSE(dev.is_valid_io(1, 4096));          // misaligned offset
    EXPECT_FALSE(dev.is_valid_io(0, 1));             // misaligned length
    EXPECT_FALSE(dev.is_valid_io(0, 4097));          // misaligned length
    EXPECT_FALSE(dev.is_valid_io(kFileSize, 4096));  // past end
    EXPECT_FALSE(dev.is_valid_io(0, kFileSize + 1));
    dev.close();
}

// ---------------------------------------------------------------------------
// Synchronous read / write
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, SyncWriteThenRead) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);

    bufferlist write_bl;
    std::string payload = "Hello KernelDevice!";
    // Pad to block-aligned length
    while (payload.size() % dev.get_block_size())
        payload.push_back('\0');
    write_bl.append(payload.c_str(), payload.size());

    int r = dev.write(0, write_bl, false, 0);
    ASSERT_EQ(r, (int)payload.size());

    bufferlist read_bl;
    r = dev.read(0, payload.size(), &read_bl, nullptr, false);
    ASSERT_EQ(r, (int)payload.size());
    EXPECT_EQ(read_bl.length(), payload.size());
    EXPECT_TRUE(read_bl.contents_equal(write_bl));

    dev.close();
}

TEST_F(KernelDeviceTest, BufferedWriteThenRead) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);

    bufferlist write_bl;
    std::string payload = "Buffered IO works!";
    // Pad to block-aligned length (block_size is 4096 for regular files)
    uint64_t bs = dev.get_block_size();
    while (payload.size() % bs)
        payload.push_back('\0');
    write_bl.append(payload.c_str(), payload.size());

    int r = dev.write(0, write_bl, true, 0);
    EXPECT_EQ(r, (int)payload.size());

    bufferlist read_bl;
    r = dev.read(0, payload.size(), &read_bl, nullptr, true);
    EXPECT_EQ(r, (int)payload.size());
    EXPECT_TRUE(read_bl.contents_equal(write_bl));

    dev.close();
}

// ---------------------------------------------------------------------------
// Flush
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, FlushNoIoIsNoop) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);
    EXPECT_EQ(dev.flush(), 0);
    dev.close();
}

TEST_F(KernelDeviceTest, FlushAfterWrite) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);

    bufferlist bl;
    std::string data = "data to flush";
    while (data.size() % dev.get_block_size())
        data.push_back('\0');
    bl.append(data.c_str(), data.size());
    ASSERT_EQ(dev.write(0, bl, false, 0), (int)data.size());
    EXPECT_EQ(dev.flush(), 0);

    dev.close();
}

// ---------------------------------------------------------------------------
// Factory integration
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, FactoryOpenClose) {
    auto dev = BlockDevice::create(tmp_path_, nullptr, nullptr);
    ASSERT_NE(dev, nullptr);
}

// ---------------------------------------------------------------------------
// Async IO smoke test
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, AioWriteThenSyncRead) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);

    IOContext ioc(&dev);
    bufferlist write_bl;
    std::string payload = "AIO write works!";
    while (payload.size() % dev.get_block_size())
        payload.push_back('\0');
    write_bl.append(payload.c_str(), payload.size());

    bufferlist expected;
    expected.append(payload.c_str(), payload.size());

    int r = dev.aio_write(0, write_bl, &ioc, false, 0);
    ASSERT_EQ(r, 0);

    dev.aio_submit(&ioc);
    ioc.aio_wait();
    EXPECT_EQ(ioc.get_return_value(), 0);

    bufferlist read_bl;
    r = dev.read(0, payload.size(), &read_bl, nullptr, false);
    ASSERT_EQ(r, (int)payload.size());
    EXPECT_TRUE(read_bl.contents_equal(expected));

    dev.close();
}

TEST_F(KernelDeviceTest, AioReadFromWrittenData) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);

    // Write data synchronously first
    bufferlist write_bl;
    std::string payload = "AIO read test!";
    while (payload.size() % dev.get_block_size())
        payload.push_back('\0');
    write_bl.append(payload.c_str(), payload.size());
    ASSERT_EQ(dev.write(0, write_bl, false, 0), (int)payload.size());

    // Now read async
    IOContext ioc(&dev);
    bufferlist read_bl;
    int r = dev.aio_read(0, payload.size(), &read_bl, &ioc);
    ASSERT_EQ(r, 0);

    dev.aio_submit(&ioc);
    ioc.aio_wait();
    EXPECT_EQ(ioc.get_return_value(), 0);

    dev.close();
}

TEST_F(KernelDeviceTest, MultipleAioWrites) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);

    IOContext ioc(&dev);
    constexpr int kNumWrites = 4;
    uint64_t off = 0;
    bufferlist blobs[kNumWrites];
    std::string expected[kNumWrites];

    for (int i = 0; i < kNumWrites; ++i) {
        std::string s = "write #" + std::to_string(i);
        while (s.size() % dev.get_block_size())
            s.push_back('\0');
        expected[i] = s;
        blobs[i].append(s.c_str(), s.size());
        bufferlist copy;
        copy.append(s.c_str(), s.size());
        dev.aio_write(off, copy, &ioc, false, 0);
        off += s.size();
    }

    dev.aio_submit(&ioc);
    ioc.aio_wait();

    // Verify by reading
    off = 0;
    for (int i = 0; i < kNumWrites; ++i) {
        bufferlist read_bl;
        bufferlist expect_bl;
        expect_bl.append(expected[i].c_str(), expected[i].size());
        int r = dev.read(off, expected[i].size(), &read_bl, nullptr, false);
        ASSERT_EQ(r, (int)expected[i].size());
        EXPECT_TRUE(read_bl.contents_equal(expect_bl));
        off += expected[i].size();
    }

    dev.close();
}

// ---------------------------------------------------------------------------
// Invalidate cache
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, InvalidateCache) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);
    int r = dev.invalidate_cache(0, 4096);
    // posix_fadvise can return 0 or error
    EXPECT_TRUE(r == 0 || r == -EINVAL || r == -ESPIPE);
    dev.close();
}

// ---------------------------------------------------------------------------
// Collect metadata
// ---------------------------------------------------------------------------
TEST_F(KernelDeviceTest, CollectMetadata) {
    KernelDevice dev(tmp_path_, nullptr, nullptr);
    ASSERT_EQ(dev.open(tmp_path_), 0);

    std::map<std::string, std::string> meta;
    int r = dev.collect_metadata("bdev_", &meta);
    ASSERT_EQ(r, 0);
    EXPECT_EQ(meta["bdev_type"], "kernel");
    EXPECT_EQ(meta["bdev_size"], std::to_string(kFileSize));

    dev.close();
}
