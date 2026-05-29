#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>

#include "safe_io.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

namespace {

void retry_close(int fd) {
    int r;
    do {
        r = close(fd);
    } while (r == -1 && errno == EINTR);
}

}  // namespace

ssize_t safe_read(int fd, void *buf, size_t count) {
    size_t cnt = 0;

    while (cnt < count) {
        ssize_t r = read(fd, static_cast<char *>(buf) + cnt, count - cnt);
        if (r <= 0) {
            if (r == 0)
                return static_cast<ssize_t>(cnt);
            if (errno == EINTR)
                continue;
            return -errno;
        }
        cnt += static_cast<size_t>(r);
    }
    return static_cast<ssize_t>(cnt);
}

ssize_t safe_recv(int fd, void *buf, size_t count) {
    return safe_read(fd, buf, count);
}

ssize_t safe_read_exact(int fd, void *buf, size_t count) {
    ssize_t ret = safe_read(fd, buf, count);
    if (ret < 0)
        return ret;
    if (static_cast<size_t>(ret) != count)
        return -EDOM;
    return 0;
}

ssize_t safe_recv_exact(int fd, void *buf, size_t count) {
    ssize_t ret = safe_recv(fd, buf, count);
    if (ret < 0)
        return ret;
    if (static_cast<size_t>(ret) != count)
        return -EDOM;
    return 0;
}

ssize_t safe_write(int fd, const void *buf, size_t count) {
    while (count > 0) {
        ssize_t r = write(fd, buf, count);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        count -= static_cast<size_t>(r);
        buf = static_cast<const char *>(buf) + r;
    }
    return 0;
}

ssize_t safe_send(int fd, const void *buf, size_t count) {
    return safe_write(fd, buf, count);
}

ssize_t safe_pread(int fd, void *buf, size_t count, off_t offset) {
    size_t cnt = 0;

    while (cnt < count) {
        ssize_t r = pread(fd, static_cast<char *>(buf) + cnt, count - cnt,
                          offset + static_cast<off_t>(cnt));
        if (r <= 0) {
            if (r == 0)
                return static_cast<ssize_t>(cnt);
            if (errno == EINTR)
                continue;
            return -errno;
        }
        cnt += static_cast<size_t>(r);
    }
    return static_cast<ssize_t>(cnt);
}

ssize_t safe_pread_exact(int fd, void *buf, size_t count, off_t offset) {
    ssize_t ret = safe_pread(fd, buf, count, offset);
    if (ret < 0)
        return ret;
    if (static_cast<size_t>(ret) != count)
        return -EDOM;
    return 0;
}

ssize_t safe_pwrite(int fd, const void *buf, size_t count, off_t offset) {
    while (count > 0) {
        ssize_t r = pwrite(fd, buf, count, offset);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        count -= static_cast<size_t>(r);
        buf = static_cast<const char *>(buf) + r;
        offset += r;
    }
    return 0;
}

int safe_write_file(const char *base, const char *file, const char *val,
                    size_t vallen, unsigned mode) {
    int ret;
    char fn[PATH_MAX];
    char tmp[PATH_MAX];
    int fd;

    char oldval[80];
    ret = safe_read_file(base, file, oldval, sizeof(oldval));
    if (ret == static_cast<int>(vallen) &&
        memcmp(oldval, val, vallen) == 0)
        return 0;

    snprintf(fn, sizeof(fn), "%s/%s", base, file);
    snprintf(tmp, sizeof(tmp), "%s/%s.tmp", base, file);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, mode);
    if (fd < 0) {
        ret = errno;
        return -ret;
    }
    ret = safe_write(fd, val, vallen);
    if (ret) {
        retry_close(fd);
        return ret;
    }

    ret = fsync(fd);
    if (ret < 0)
        ret = -errno;
    retry_close(fd);
    if (ret < 0) {
        unlink(tmp);
        return ret;
    }
    ret = rename(tmp, fn);
    if (ret < 0) {
        ret = -errno;
        unlink(tmp);
        return ret;
    }

    fd = open(base, O_RDONLY | O_BINARY);
    if (fd < 0) {
        ret = -errno;
        return ret;
    }
    ret = fsync(fd);
    if (ret < 0)
        ret = -errno;
    retry_close(fd);

    return ret;
}

int safe_read_file(const char *base, const char *file, char *val,
                   size_t vallen) {
    char fn[PATH_MAX];
    int fd, len;

    snprintf(fn, sizeof(fn), "%s/%s", base, file);
    fd = open(fn, O_RDONLY | O_BINARY);
    if (fd < 0)
        return -errno;

    len = safe_read(fd, val, vallen);
    if (len < 0) {
        retry_close(fd);
        return len;
    }
    retry_close(fd);

    return len;
}
