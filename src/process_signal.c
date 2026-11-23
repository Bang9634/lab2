#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t received;

static void on_signal(int signal_number)
{
    received = signal_number;
}

int main(void)
{
    struct sigaction action = { .sa_handler = on_signal };
    sigset_t block, saved, waiting;
    sigemptyset(&action.sa_mask);
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    if (sigaction(SIGUSR1, &action, NULL) < 0 ||
        sigprocmask(SIG_BLOCK, &block, &saved) < 0) {
        perror("signal setup");
        return 1;
    }
    waiting = saved;
    sigdelset(&waiting, SIGUSR1);

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }
    if (child == 0) {
        while (!received)
            sigsuspend(&waiting);
        if (sigprocmask(SIG_SETMASK, &saved, NULL) < 0)
            _exit(1);
        printf("child pid=%ld parent=%ld received SIGUSR1\n",
               (long)getpid(), (long)getppid());
        fflush(stdout);
        execlp("printf", "printf", "%s\n", "child: exec completed", (char *)NULL);
        perror("execlp");
        _exit(127);
    }

    printf("parent pid=%ld sending SIGUSR1 to %ld\n", (long)getpid(), (long)child);
    fflush(stdout);
    if (kill(child, SIGUSR1) < 0)
        perror("kill");
    int status;
    pid_t reaped;
    do {
        reaped = waitpid(child, &status, 0);
    } while (reaped < 0 && errno == EINTR);
    if (reaped < 0) {
        perror("waitpid");
        return 1;
    }
    if (!WIFEXITED(status)) {
        fprintf(stderr, "child did not exit normally\n");
        return 1;
    }
    printf("parent: waitpid collected exit=%d\n", WEXITSTATUS(status));
    return WEXITSTATUS(status);
}
