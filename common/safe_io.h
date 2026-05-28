#ifndef COMMON_SAFE_IO_H
#define COMMON_SAFE_IO_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

[[nodiscard]] ssize_t safe_read(int fd, void *buf, size_t count);
[[nodiscard]] ssize_t safe_write(int fd, const void *buf, size_t count);
[[nodiscard]] ssize_t safe_recv(int fd, void *buf, size_t count);
[[nodiscard]] ssize_t safe_send(int fd, const void *buf, size_t count);
[[nodiscard]] ssize_t safe_pread(int fd, void *buf, size_t count, off_t offset);
[[nodiscard]] ssize_t safe_pwrite(int fd, const void *buf, size_t count,
                                  off_t offset);
[[nodiscard]] ssize_t safe_read_exact(int fd, void *buf, size_t count);
[[nodiscard]] ssize_t safe_recv_exact(int fd, void *buf, size_t count);
[[nodiscard]] ssize_t safe_pread_exact(int fd, void *buf, size_t count,
                                       off_t offset);

int safe_write_file(const char *base, const char *file, const char *val,
                    size_t vallen, unsigned mode);
int safe_read_file(const char *base, const char *file, char *val,
                   size_t vallen);

#ifdef __cplusplus
}
#endif

#endif  // COMMON_SAFE_IO_H
