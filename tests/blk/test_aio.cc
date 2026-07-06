#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <list>
#include <vector>

#include "blk/aio.h"
#include "cxxlab_test.h"

using namespace TOPNSPC;

class AioTest : public ::testing::Test {
protected:
    int fd_ = -1;

    void SetUp() override {
        auto tmpl = cxxlab_tmp_path("aio");
        fd_ = mkstemp(tmpl.data());
        ASSERT_GE(fd_, 0) << "mkstemp failed";
        ASSERT_EQ(0, unlink(tmpl.c_str()));
        ASSERT_EQ(ftruncate(fd_, 1048576), 0);
    }

    void TearDown() override {
        if (fd_ >= 0)
            close(fd_);
    }

    void submit_and_drain(aio_queue_t &q, std::list<aio_t> &aios) {
        int expect = aios.size();
        if (expect == 0)
            return;
        int retries = 10;
        int r = q.submit_batch(aios.begin(), aios.end(), nullptr, &retries);
        ASSERT_EQ(r, expect);

        int done = 0;
        for (int i = 0; i < 30 && done < expect; i++) {
            aio_t *ps[16] = {};
            int n = q.get_next_completed(200, ps, 16);
            ASSERT_GE(n, 0);
            done += n;
        }
        ASSERT_EQ(done, expect);
    }
};

TEST_F(AioTest, ReadWriteRoundtrip) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    char wbuf[4096];
    char rbuf[4096] = {};
    for (int i = 0; i < 4096; i++)
        wbuf[i] = i & 0xFF;

    std::list<aio_t> aios;
    aios.emplace_back(nullptr, fd_);
    auto &aio_w = aios.back();
    aio_w.iov.push_back({wbuf, 4096});
    aio_w.pwritev(0, 4096);

    submit_and_drain(queue, aios);
    EXPECT_GE(aio_w.rval, 0);

    aios.clear();
    aios.emplace_back(nullptr, fd_);
    auto &aio_r = aios.back();
    aio_r.iov.push_back({rbuf, 4096});
    aio_r.preadv(0, 4096);

    submit_and_drain(queue, aios);
    EXPECT_EQ(aio_r.rval, 4096);
    EXPECT_EQ(memcmp(wbuf, rbuf, 4096), 0);

    queue.shutdown();
}

TEST_F(AioTest, MultiBufferIov) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    char buf_a[512], buf_b[512], out[1024];
    memset(buf_a, 0xAB, 512);
    memset(buf_b, 0xCD, 512);

    std::list<aio_t> aios;
    aios.emplace_back(nullptr, fd_);
    auto &w = aios.back();
    w.iov.push_back({buf_a, 512});
    w.iov.push_back({buf_b, 512});
    w.pwritev(0, 1024);
    submit_and_drain(queue, aios);
    ASSERT_GE(w.rval, 0);

    aios.clear();
    aios.emplace_back(nullptr, fd_);
    auto &r = aios.back();
    r.iov.push_back({out, 1024});
    r.preadv(0, 1024);
    submit_and_drain(queue, aios);
    EXPECT_EQ(r.rval, 1024);
    EXPECT_EQ(memcmp(out, buf_a, 512), 0);
    EXPECT_EQ(memcmp(out + 512, buf_b, 512), 0);

    queue.shutdown();
}

TEST_F(AioTest, BatchSubmit) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    char wbuf[4096];
    for (int i = 0; i < 4096; i++)
        wbuf[i] = i & 0xFF;

    std::list<aio_t> aios;
    for (int i = 0; i < 4; i++) {
        aios.emplace_back(nullptr, fd_);
        auto &a = aios.back();
        a.iov.push_back({wbuf + i * 1024, 1024});
        a.pwritev(i * 1024, 1024);
    }

    submit_and_drain(queue, aios);
    for (auto &a : aios)
        EXPECT_GE(a.rval, 0) << "IO at offset " << a.offset << " failed";

    char rbuf[4096];
    aios.clear();
    aios.emplace_back(nullptr, fd_);
    auto &r = aios.back();
    r.iov.push_back({rbuf, 4096});
    r.preadv(0, 4096);
    submit_and_drain(queue, aios);
    EXPECT_EQ(r.rval, 4096);
    EXPECT_EQ(memcmp(wbuf, rbuf, 4096), 0);

    queue.shutdown();
}

TEST_F(AioTest, EmptyBatch) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    std::list<aio_t> aios;
    int retries = 10;
    int r = queue.submit_batch(aios.begin(), aios.end(), nullptr, &retries);
    EXPECT_EQ(r, 0);

    queue.shutdown();
}

TEST_F(AioTest, NonBlockingPoll) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    aio_t *ps[4] = {};
    int n = queue.get_next_completed(0, ps, 4);
    EXPECT_EQ(n, 0);

    queue.shutdown();
}

TEST_F(AioTest, InvalidFd) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    close(fd_);
    fd_ = -1;

    char buf[256] = {};
    std::list<aio_t> aios;
    aios.emplace_back(nullptr, -1);
    auto &a = aios.back();
    a.iov.push_back({buf, 256});
    a.preadv(0, 256);

    int retries = 10;
    int r = queue.submit_batch(aios.begin(), aios.end(), nullptr, &retries);
    if (r == 1) {
        aio_t *ps[4] = {};
        int n = queue.get_next_completed(2000, ps, 4);
        ASSERT_EQ(n, 1);
        EXPECT_LT(ps[0]->rval, 0) << "expected error on invalid fd";
    } else {
        EXPECT_LT(r, 0) << "expected submission to report error";
    }

    queue.shutdown();
}

TEST_F(AioTest, DoubleInit) {
    aio_queue_t queue(1);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);
    ASSERT_EQ(queue.init(fds), 0);
    queue.shutdown();
}

TEST_F(AioTest, ShutdownWithoutInit) {
    aio_queue_t queue(16);
    queue.shutdown();
}

TEST_F(AioTest, ReinitAfterShutdown) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);
    queue.shutdown();
    ASSERT_EQ(queue.init(fds), 0);

    char wbuf[256] = {};
    std::list<aio_t> aios;
    aios.emplace_back(nullptr, fd_);
    auto &a = aios.back();
    a.iov.push_back({wbuf, 256});
    a.pwritev(0, 256);
    submit_and_drain(queue, aios);
    EXPECT_GE(a.rval, 0);

    queue.shutdown();
}

TEST_F(AioTest, SmallIodepth) {
    aio_queue_t queue(1);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    char buf[256] = {};
    std::list<aio_t> aios;
    aios.emplace_back(nullptr, fd_);
    auto &a = aios.back();
    a.iov.push_back({buf, 256});
    a.pwritev(0, 256);
    submit_and_drain(queue, aios);
    EXPECT_GE(a.rval, 0);

    queue.shutdown();
}

TEST_F(AioTest, ZeroLength) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    char buf[1] = {};
    std::list<aio_t> aios;
    aios.emplace_back(nullptr, fd_);
    auto &a = aios.back();
    a.iov.push_back({buf, 0});
    a.pwritev(0, 0);
    submit_and_drain(queue, aios);
    EXPECT_GE(a.rval, 0);
    EXPECT_EQ(a.rval, 0);

    queue.shutdown();
}

TEST_F(AioTest, LargeOffset) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    char buf[256] = {};
    memset(buf, 0x42, 256);

    std::list<aio_t> aios;
    aios.emplace_back(nullptr, fd_);
    auto &w = aios.back();
    w.iov.push_back({buf, 256});
    w.pwritev(65536, 256);
    submit_and_drain(queue, aios);
    EXPECT_GE(w.rval, 0);

    char rbuf[256] = {};
    aios.clear();
    aios.emplace_back(nullptr, fd_);
    auto &r = aios.back();
    r.iov.push_back({rbuf, 256});
    r.preadv(65536, 256);
    submit_and_drain(queue, aios);
    EXPECT_EQ(r.rval, 256);
    EXPECT_EQ(memcmp(buf, rbuf, 256), 0);

    queue.shutdown();
}

TEST_F(AioTest, ReadIntoMultipleBuffers) {
    aio_queue_t queue(16);
    std::vector<int> fds;
    ASSERT_EQ(queue.init(fds), 0);

    char src[1024];
    for (int i = 0; i < 1024; i++)
        src[i] = i & 0xFF;

    std::list<aio_t> aios;
    aios.emplace_back(nullptr, fd_);
    aios.back().iov.push_back({src, 1024});
    aios.back().pwritev(0, 1024);
    submit_and_drain(queue, aios);
    ASSERT_GE(aios.front().rval, 0);

    char d0[256], d1[256], d2[256], d3[256];
    aios.clear();
    aios.emplace_back(nullptr, fd_);
    auto &r = aios.back();
    r.iov.push_back({d0, 256});
    r.iov.push_back({d1, 256});
    r.iov.push_back({d2, 256});
    r.iov.push_back({d3, 256});
    r.preadv(0, 1024);
    submit_and_drain(queue, aios);
    EXPECT_EQ(r.rval, 1024);
    EXPECT_EQ(memcmp(d0, src, 256), 0);
    EXPECT_EQ(memcmp(d1, src + 256, 256), 0);
    EXPECT_EQ(memcmp(d2, src + 512, 256), 0);
    EXPECT_EQ(memcmp(d3, src + 768, 256), 0);

    queue.shutdown();
}
