#ifndef BLK_BLOCK_DEVICE_H
#define BLK_BLOCK_DEVICE_H

#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "common/buffer_fwd.h"
#include "common/common_fwd.h"

namespace TOPNSPC {

struct IOContext;

// Write life time hints (Linux F_SET_FILE_RW_HINT)
enum {
    WRITE_LIFE_NOT_SET = 0,
    WRITE_LIFE_NONE = 1,
    WRITE_LIFE_SHORT = 2,
    WRITE_LIFE_MEDIUM = 3,
    WRITE_LIFE_LONG = 4,
    WRITE_LIFE_EXTREME = 5,
    WRITE_LIFE_MAX = 6,
};

class BlockDevice {
public:
    using aio_callback_t = void (*)(void *handle, void *aio);

    aio_callback_t aio_callback = nullptr;
    void *aio_callback_priv = nullptr;

    BlockDevice(aio_callback_t cb, void *cbpriv)
        : aio_callback(cb), aio_callback_priv(cbpriv) {}
    virtual ~BlockDevice() = default;

    // Factory — always creates KernelDevice on this platform
    static std::unique_ptr<BlockDevice> create(
        const std::string &path, aio_callback_t cb, void *cbpriv);

    // Open / close
    virtual int open(const std::string &path) = 0;
    virtual void close() = 0;

    // Synchronous IO
    virtual int read(uint64_t off, uint64_t len,
                     bufferlist *pbl, IOContext *ioc,
                     bool buffered) = 0;
    virtual int read_random(uint64_t off, uint64_t len,
                            char *buf, bool buffered) = 0;
    virtual int write(uint64_t off, bufferlist &bl,
                      bool buffered,
                      int write_hint = WRITE_LIFE_NOT_SET) = 0;
    virtual int flush() = 0;

    // Async IO
    virtual int aio_read(uint64_t off, uint64_t len,
                         bufferlist *pbl, IOContext *ioc) = 0;
    virtual int aio_write(uint64_t off, bufferlist &bl,
                          IOContext *ioc, bool buffered,
                          int write_hint = WRITE_LIFE_NOT_SET) = 0;
    virtual void aio_submit(IOContext *ioc) = 0;
    virtual int discard(uint64_t off, uint64_t len) = 0;

    // Cache management
    virtual int invalidate_cache(uint64_t off, uint64_t len) = 0;

    // Metadata
    virtual int collect_metadata(
        const std::string &prefix,
        std::map<std::string, std::string> *pm) const = 0;
    virtual int get_devname(std::string *out) const {
        return -ENOENT;
    }

    // Properties
    uint64_t get_size() const { return size; }
    uint64_t get_block_size() const { return block_size; }
    uint64_t get_optimal_io_size() const { return optimal_io_size; }
    bool is_rotational() const { return rotational; }
    bool support_discard() const { return support_discard_; }

    bool is_valid_io(uint64_t off, uint64_t len) const;

protected:
    uint64_t size = 0;
    uint64_t block_size = 0;
    uint64_t optimal_io_size = 0;
    bool support_discard_ = false;
    bool rotational = true;
};

}  // namespace TOPNSPC

#endif  // BLK_BLOCK_DEVICE_H
