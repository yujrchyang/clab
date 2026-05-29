#ifndef BLK_KERNEL_DEVICE_H
#define BLK_KERNEL_DEVICE_H

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "blk/aio.h"
#include "blk/block_device.h"

class KernelDevice : public BlockDevice {
public:
    explicit KernelDevice(const std::string &path,
                          aio_callback_t cb, void *cbpriv)
        : BlockDevice(cb, cbpriv), path_(path) {}
    ~KernelDevice() override;

    int open(const std::string &path) override;
    void close() override;

    int read(uint64_t off, uint64_t len, TOPNSPC::bufferlist *pbl,
             IOContext *ioc, bool buffered) override;
    int read_random(uint64_t off, uint64_t len, char *buf,
                    bool buffered) override;
    int write(uint64_t off, TOPNSPC::bufferlist &bl, bool buffered,
              int write_hint) override;
    int flush() override;

    int aio_read(uint64_t off, uint64_t len, TOPNSPC::bufferlist *pbl,
                 IOContext *ioc) override;
    int aio_write(uint64_t off, TOPNSPC::bufferlist &bl, IOContext *ioc,
                  bool buffered, int write_hint) override;
    void aio_submit(IOContext *ioc) override;
    int discard(uint64_t off, uint64_t len) override;

    int invalidate_cache(uint64_t off, uint64_t len) override;

    int collect_metadata(const std::string &prefix,
                         std::map<std::string, std::string> *pm)
        const override;

private:
    void _aio_thread();

    std::string path_;
    int fd_direct_ = -1;
    int fd_buffered_ = -1;
    bool dio_ = true;
    bool aio_ = true;

    std::unique_ptr<aio_queue_t> io_queue_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> io_since_flush_{false};
    std::mutex flush_mutex_;
    std::thread aio_thread_;
};

#endif  // BLK_KERNEL_DEVICE_H
