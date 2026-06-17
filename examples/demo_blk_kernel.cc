#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "blk/block_device.h"
#include "blk/io_context.h"
#include "common/buffer.h"

using namespace TOPNSPC;

static constexpr uint64_t kFileSize = 4 << 20;  // 4 MiB

static std::string make_tmpfile() {
    auto path = std::filesystem::temp_directory_path() / "blk_demo_XXXXXX";
    auto s = path.string();
    int fd = mkstemp(s.data());
    if (fd < 0) {
        std::cerr << "mkstemp failed\n";
        std::abort();
    }
    int r = fallocate(fd, 0, 0, kFileSize);
    if (r < 0) {
        std::vector<char> zeros(4096, 0);
        for (uint64_t off = 0; off < kFileSize; off += zeros.size()) {
            auto chunk = std::min<uint64_t>(zeros.size(), kFileSize - off);
            ::pwrite(fd, zeros.data(), chunk, static_cast<off_t>(off));
        }
    }
    ::close(fd);
    return s;
}

static std::string make_payload(uint64_t block_size) {
    std::string s = "Hello from clab block device demo!";
    while (s.size() % block_size)
        s.push_back('\0');
    return s;
}

int main() {
    std::string path = make_tmpfile();

    // 1. Create and open
    auto dev = BlockDevice::create(path, nullptr, nullptr);
    assert(dev);
    int r = dev->open(path);
    assert(r == 0);
    uint64_t bs = dev->get_block_size();
    std::cout << "block_size=" << bs << " size=" << dev->get_size() << "\n";

    std::string payload = make_payload(bs);
    uint64_t len = payload.size();

    // 2. Synchronous write + read
    {
        bufferlist wbl;
        wbl.append(payload.data(), static_cast<unsigned>(len));
        r = dev->write(0, wbl, false, 0);
        std::cout << "sync write: " << r << " bytes\n";

        bufferlist rbl;
        r = dev->read(0, len, &rbl, nullptr, false);
        std::cout << "sync read: " << r << " bytes\n";
        assert(rbl.contents_equal(wbl));
    }

    // 3. Async write
    {
        IOContext ioc(dev.get());
        bufferlist wbl;
        wbl.append(payload.data(), static_cast<unsigned>(len));
        r = dev->aio_write(0, wbl, &ioc, false, 0);
        assert(r == 0);
        dev->aio_submit(&ioc);
        ioc.aio_wait();
        std::cout << "async write: return_value=" << ioc.get_return_value()
                  << "\n";
    }

    // Verify with sync read
    {
        bufferlist rbl;
        r = dev->read(0, len, &rbl, nullptr, false);
        assert(r == static_cast<int>(len));
        assert(rbl.to_str() == payload);
    }

    // 4. Async read (after sync write)
    {
        bufferlist wbl;
        wbl.append(payload.data(), static_cast<unsigned>(len));
        r = dev->write(0, wbl, false, 0);
        assert(r == static_cast<int>(len));

        IOContext ioc(dev.get());
        bufferlist rbl;
        r = dev->aio_read(0, len, &rbl, &ioc);
        assert(r == 0);
        dev->aio_submit(&ioc);
        ioc.aio_wait();
        std::cout << "async read: return_value=" << ioc.get_return_value()
                  << "\n";
        assert(rbl.contents_equal(wbl));
    }

    // 5. Close
    dev->close();
    std::filesystem::remove(path);
    std::cout << "OK\n";
    return 0;
}
