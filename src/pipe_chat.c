#include "chat.h"
#include "io.h"
#include <signal.h>
#include <sys/wait.h>

static int send_turn(int fd, const char *role)
{
    char message[CHAT_SIZE] = {0};
    if (chat_input(role, message) < 0 ||
        write_all(fd, message, sizeof(message)) < 0)
        return -1;
    return strcmp(message, "/quit") != 0;
}

static int receive_turn(int fd, const char *peer)
{
    char message[CHAT_SIZE];
    int result = read_exact(fd, message, sizeof(message));
    if (result == 1) {
        printf("received from %s: %s\n", peer, message);
        fflush(stdout);
        return strcmp(message, "/quit") != 0;
    }
    return result;
}

int main(void)
{
    /* fd 0 is shared after fork. Read only on the current role's turn. */
    if (setvbuf(stdin, NULL, _IONBF, 0) != 0 || signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return 1;

    int to_b[2], to_a[2];
    if (pipe(to_b) < 0) {
        perror("pipe");
        return 1;
    }
    if (pipe(to_a) < 0) {
        perror("pipe");
        return 1;
    }
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }

    int result;
    if (child == 0) {
        close(to_b[1]);
        close(to_a[0]);
        while ((result = receive_turn(to_b[0], "A")) > 0 &&
               (result = send_turn(to_a[1], "B")) > 0) {
        }
        if (result < 0)
            perror("B");
        close(to_b[0]);
        close(to_a[1]);
        _exit(result < 0);
    }

    close(to_b[0]);
    close(to_a[1]);
    puts("Two processes share this terminal. Type alternately. /quit ends the chat.");
    while ((result = send_turn(to_b[1], "A")) > 0 &&
           (result = receive_turn(to_a[0], "B")) > 0) {
    }
    if (result < 0)
        perror("A");
    close(to_b[1]);
    close(to_a[0]);
    int status;
    pid_t reaped;
    do {
        reaped = waitpid(child, &status, 0);
    } while (reaped < 0 && errno == EINTR);
    return result < 0 || reaped != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}
