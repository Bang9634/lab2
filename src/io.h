#ifndef LAB2_IO_H
#define LAB2_IO_H

#include <errno.h>
#include <stddef.h>
#include <unistd.h>

static inline int write_all(int fd, const void *data, size_t size)
{
    const char *bytes = data;
    while (size > 0) {
        ssize_t count = write(fd, bytes, size);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        bytes += count;
        size -= (size_t)count;
    }
    return 0;
}

/* Return 1 for a complete record, 0 for EOF, and -1 for an error. */
static inline int read_exact(int fd, void *data, size_t size)
{
    char *bytes = data;
    for (size_t done = 0; done < size;) {
        ssize_t count = read(fd, bytes + done, size - done);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            return -1;
        if (count == 0) {
            if (done == 0)
                return 0;
            errno = EIO;
            return -1;
        }
        done += (size_t)count;
    }
    return 1;
}

#endif
