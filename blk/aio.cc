#include <cerrno>
#include <ostream>
#include <vector>

#include "blk/aio.h"

// ---------------------------------------------------------------------------
// aio_t
// ---------------------------------------------------------------------------
aio_t::aio_t(void *p, int f) : priv(p), fd(f) {}

void aio_t::pwritev(uint64_t _offset, uint64_t len) {
    offset = _offset;
    length = len;
    io_prep_pwritev(&iocb, fd, iov.data(), iov.size(), _offset);
    iocb.data = this;
}

void aio_t::preadv(uint64_t _offset, uint64_t len) {
    offset = _offset;
    length = len;
    io_prep_preadv(&iocb, fd, iov.data(), iov.size(), _offset);
    iocb.data = this;
}

std::ostream &operator<<(std::ostream &os, const aio_t &aio) {
    os << "aio_t(fd=" << aio.fd << " offset=" << aio.offset
       << " length=" << aio.length << " rval=" << aio.rval << ")";
    return os;
}

// ---------------------------------------------------------------------------
// aio_queue_t
// ---------------------------------------------------------------------------
aio_queue_t::aio_queue_t(unsigned d) : max_iodepth(d) {}

aio_queue_t::~aio_queue_t() {
    shutdown();
}

int aio_queue_t::init(std::vector<int> &) {
    if (ctx) {
        io_destroy(ctx);
        ctx = nullptr;
    }
    int r = io_setup(max_iodepth, &ctx);
    if (r < 0) {
        ctx = nullptr;
        return -r;
    }
    return 0;
}

void aio_queue_t::shutdown() {
    if (!ctx)
        return;
    io_destroy(ctx);
    ctx = nullptr;
}

int aio_queue_t::submit_batch(aio_iter begin, aio_iter end,
                              void *priv, int *retries) {
    std::vector<struct iocb *> iocbs;
    iocbs.reserve(std::distance(begin, end));
    for (auto it = begin; it != end; ++it) {
        it->iocb.data = priv;
        iocbs.push_back(&it->iocb);
    }

    int r = 0;
    auto p = iocbs.data();
    auto left = iocbs.size();
    while (left > 0) {
        int submitted = io_submit(ctx, left, p);
        if (submitted < 0) {
            int err = -submitted;
            if (err == EAGAIN && *retries > 0) {
                --(*retries);
                continue;
            }
            if (r == 0)
                r = -err;
            break;
        }
        p += submitted;
        left -= submitted;
        r += submitted;
    }
    return r;
}

int aio_queue_t::get_next_completed(int timeout_ms,
                                    aio_t **paio, int max) {
    struct timespec ts {};
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000LL;

    std::vector<struct io_event> events(max);
    int r;
    do {
        r = io_getevents(ctx, 1, max, events.data(), &ts);
    } while (r == -EINTR);
    if (r < 0)
        return -r;

    for (int i = 0; i < r; ++i) {
        auto *aio = reinterpret_cast<aio_t *>(events[i].obj);
        aio->rval = events[i].res;
        paio[i] = aio;
    }
    return r;
}
