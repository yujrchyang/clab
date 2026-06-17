#ifndef BLK_AIO_H
#define BLK_AIO_H

#include <libaio.h>

#include <cstdint>
#include <list>
#include <ostream>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/intrusive/list.hpp>

#include "common/buffer.h"
#include "common/common_fwd.h"

namespace TOPNSPC {

// ---------------------------------------------------------------------------
// aio_t — single asynchronous I/O operation
// ---------------------------------------------------------------------------
struct aio_t {
    struct iocb iocb {};

    void *priv;
    int fd;
    boost::container::small_vector<struct iovec, 4> iov;
    uint64_t offset = 0;
    uint64_t length = 0;
    long rval = -1000;
    bufferlist bl;  // holds data stable for write duration

    boost::intrusive::list_member_hook<> queue_item;

    aio_t(void *p, int f);
    void pwritev(uint64_t _offset, uint64_t len);
    void preadv(uint64_t _offset, uint64_t len);
    long get_return_value() const { return rval; }
};

std::ostream &operator<<(std::ostream &os, const aio_t &aio);

using aio_list_t = boost::intrusive::list<
    aio_t,
    boost::intrusive::member_hook<
        aio_t,
        boost::intrusive::list_member_hook<>,
        &aio_t::queue_item>>;

// ---------------------------------------------------------------------------
// io_queue_t / aio_queue_t — abstract + libaio queue
// ---------------------------------------------------------------------------
struct io_queue_t {
    using aio_iter = std::list<aio_t>::iterator;

    virtual ~io_queue_t() = default;

    virtual int init(std::vector<int> &fds) = 0;
    virtual void shutdown() = 0;
    virtual int submit_batch(aio_iter begin, aio_iter end,
                             void *priv, int *retries) = 0;
    virtual int get_next_completed(int timeout_ms,
                                   aio_t **paio, int max) = 0;
};

struct aio_queue_t final : public io_queue_t {
    unsigned max_iodepth;
    io_context_t ctx = nullptr;

    explicit aio_queue_t(unsigned max_iodepth);
    ~aio_queue_t() final;

    int init(std::vector<int> &fds) final;
    void shutdown() final;
    int submit_batch(aio_iter begin, aio_iter end,
                     void *priv, int *retries) final;
    int get_next_completed(int timeout_ms,
                           aio_t **paio, int max) final;
};

}  // namespace TOPNSPC

#endif  // BLK_AIO_H
