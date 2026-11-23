#include "io.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define BUFFER_SIZE (64 * 1024)

static void *create_buffer(void)
{
    char name[64];
    snprintf(name, sizeof(name), "/lab2_copy_%ld", (long)getpid());
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return MAP_FAILED;
    /* The mapping survives closing the descriptor and unlinking the name. */
    void *buffer = MAP_FAILED;
    if (shm_unlink(name) == 0 && ftruncate(fd, BUFFER_SIZE) == 0)
        buffer = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    int saved = errno;
    close(fd);
    errno = saved;
    return buffer;
}

static int read_source(int source, void *buffer, int ready, int ack)
{
    for (;;) {
        ssize_t count = read(source, buffer, BUFFER_SIZE);
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 || write_all(ready, &count, sizeof(count)) < 0)
            return -1;
        if (count == 0)
            return 0;
        char confirmation;
        int result = read_exact(ack, &confirmation, 1);
        if (result != 1) {
            if (result == 0)
                errno = EPIPE;
            return -1;
        }
    }
}

static int write_destination(int destination, const void *buffer, int ready, int ack)
{
    for (;;) {
        ssize_t count;
        int result = read_exact(ready, &count, sizeof(count));
        if (result != 1) {
            if (result == 0)
                errno = EPIPE;
            return -1;
        }
        if (count == 0)
            return 0;
        if (write_all(destination, buffer, (size_t)count) < 0 ||
            write_all(ack, "1", 1) < 0)
            return -1;
    }
}

static int transfer(int source, int destination)
{
    void *buffer = create_buffer();
    if (buffer == MAP_FAILED) {
        perror("shared memory");
        return -1;
    }
    int ready[2] = {-1, -1}, ack[2] = {-1, -1};
    int result = -1;
    if (pipe(ready) < 0 || pipe(ack) < 0) {
        perror("pipe");
        goto cleanup;
    }

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        goto cleanup;
    }
    if (child == 0) {
        close(source);
        close(ready[1]);
        close(ack[0]);
        int written = write_destination(destination, buffer, ready[0], ack[1]);
        if (written < 0)
            perror("copy destination");
        _exit(written < 0);
    }

    close(ready[0]);
    close(ack[1]);
    result = read_source(source, buffer, ready[1], ack[0]);
    if (result < 0)
        perror("copy source or acknowledgement");
    close(ready[1]);
    close(ack[0]);
    ready[0] = ready[1] = ack[0] = ack[1] = -1;
    int status;
    pid_t reaped;
    do {
        reaped = waitpid(child, &status, 0);
    } while (reaped < 0 && errno == EINTR);
    if (reaped != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
        result = -1;

cleanup:
    for (size_t i = 0; i < 2; ++i) {
        if (ready[i] >= 0)
            close(ready[i]);
        if (ack[i] >= 0)
            close(ack[i]);
    }
    munmap(buffer, BUFFER_SIZE);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s SOURCE DESTINATION\n", argv[0]);
        return 2;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }
    int source = open(argv[1], O_RDONLY | O_NONBLOCK);
    struct stat from, to;
    if (source < 0 || fstat(source, &from) < 0) {
        perror(argv[1]);
        return 1;
    }
    if (!S_ISREG(from.st_mode)) {
        fputs("Source must be a regular file.\n", stderr);
        close(source);
        return 1;
    }

    if (lstat(argv[2], &to) == 0) {
        if (!S_ISREG(to.st_mode) || (from.st_dev == to.st_dev && from.st_ino == to.st_ino)) {
            fputs("Destination must be a different regular file.\n", stderr);
            close(source);
            return 1;
        }
    } else if (errno != ENOENT) {
        perror(argv[2]);
        close(source);
        return 1;
    }

    size_t length = strlen(argv[2]) + sizeof(".lab2-XXXXXX");
    char *temporary = malloc(length);
    if (!temporary) {
        perror("malloc");
        close(source);
        return 1;
    }
    snprintf(temporary, length, "%s.lab2-XXXXXX", argv[2]);
    int destination = mkstemp(temporary);
    int result = 1;
    if (destination < 0) {
        perror("mkstemp");
    } else {
        int copied = transfer(source, destination);
        int closed = close(destination);
        if (closed < 0)
            perror("close destination");
        if (copied == 0 && closed == 0) {
            if (rename(temporary, argv[2]) == 0) {
                printf("Copied %s -> %s using POSIX shared memory.\n", argv[1], argv[2]);
                result = 0;
            } else {
                perror("rename");
            }
        }
        if (result && unlink(temporary) < 0)
            perror("unlink partial copy");
    }
    close(source);
    free(temporary);
    return result;
}
