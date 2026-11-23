#include "io.h"
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>

int main(void)
{
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }
    char directory[] = "./fifo-demo-XXXXXX";
    if (!mkdtemp(directory)) {
        perror("mkdtemp");
        return 1;
    }
    char path[sizeof(directory) + sizeof("/channel")];
    snprintf(path, sizeof(path), "%s/channel", directory);
    if (mkfifo(path, 0600) < 0) {
        perror("mkfifo");
        rmdir(directory);
        return 1;
    }
    printf("created FIFO %s\n", path);
    fflush(stdout);

    const char message[] = "Message through a named pipe";
    pid_t child = fork();
    int result = 1;
    if (child < 0) {
        perror("fork");
    } else if (child == 0) {
        char received[sizeof(message)];
        int fd = open(path, O_RDONLY);
        if (fd < 0 || read_exact(fd, received, sizeof(received)) != 1) {
            fputs("FIFO read failed\n", stderr);
            _exit(1);
        }
        printf("reader received: %s\n", received);
        fflush(stdout);
        close(fd);
        _exit(0);
    } else {
        int fd = open(path, O_WRONLY);
        int sent = fd >= 0 && write_all(fd, message, sizeof(message)) == 0;
        if (!sent)
            perror("FIFO write");
        if (fd >= 0)
            close(fd);
        else
            kill(child, SIGTERM);
        int status;
        pid_t reaped;
        do {
            reaped = waitpid(child, &status, 0);
        } while (reaped < 0 && errno == EINTR);
        result = !(sent && reaped == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    if (unlink(path) < 0 || rmdir(directory) < 0) {
        perror("FIFO cleanup");
        result = 1;
    }
    if (result == 0)
        puts("FIFO removed.");
    return result;
}
