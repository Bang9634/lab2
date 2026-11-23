#include "chat.h"
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <time.h>

static volatile sig_atomic_t interrupted;

static void on_interrupt(int signal_number)
{
    interrupted = signal_number;
}

static int send_text(mqd_t queue, const char *message)
{
    return mq_send(queue, message, strlen(message) + 1, 0);
}

static int receive_text(mqd_t queue, char message[CHAT_SIZE])
{
    if (interrupted) {
        errno = EINTR;
        return -1;
    }

    /* A bounded wait also covers interruption between the flag check and receive. */
    for (;;) {
        struct timespec deadline;
        if (clock_gettime(CLOCK_REALTIME, &deadline) < 0)
            return -1;
        deadline.tv_sec++;
        ssize_t size = mq_timedreceive(queue, message, CHAT_SIZE, NULL, &deadline);
        if (size > 0 && message[size - 1] == '\0')
            return 0;
        if (size >= 0) {
            errno = EBADMSG;
            return -1;
        }
        if (errno != ETIMEDOUT || interrupted)
            return -1;
    }
}

static int valid_room(const char *room)
{
    const char *valid = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    size_t length = strlen(room);
    return length > 0 && length <= 40 && strspn(room, valid) == length;
}

int main(int argc, char **argv)
{
    if (argc != 3 || !valid_room(argv[2]) ||
        (strcmp(argv[1], "A") && strcmp(argv[1], "B"))) {
        fprintf(stderr, "Usage: %s A|B ROOM (1-40 letters, digits, _ or -)\n", argv[0]);
        return 2;
    }
    struct sigaction handler = { .sa_handler = on_interrupt };
    sigemptyset(&handler.sa_mask);
    if (sigaction(SIGINT, &handler, NULL) < 0 || sigaction(SIGTERM, &handler, NULL) < 0) {
        perror("sigaction");
        return 1;
    }

    int owner = strcmp(argv[1], "A") == 0;
    char a_name[64], b_name[64];
    snprintf(a_name, sizeof(a_name), "/lab2_%s_a", argv[2]);
    snprintf(b_name, sizeof(b_name), "/lab2_%s_b", argv[2]);
    struct mq_attr settings = { .mq_maxmsg = 4, .mq_msgsize = CHAT_SIZE };
    int creation = owner ? O_CREAT | O_EXCL : 0;
    mqd_t a_queue = mq_open(a_name, O_RDWR | creation, 0600, &settings);
    if (a_queue == (mqd_t)-1) {
        perror(a_name);
        return 1;
    }
    mqd_t b_queue = mq_open(b_name, O_RDWR | creation, 0600, &settings);
    if (b_queue == (mqd_t)-1) {
        perror(b_name);
        mq_close(a_queue);
        if (owner)
            mq_unlink(a_name);
        return 1;
    }

    mqd_t outgoing = owner ? a_queue : b_queue;
    mqd_t incoming = owner ? b_queue : a_queue;
    char message[CHAT_SIZE];
    int result = 1;

    if (owner) {
        puts("Queues ready. Start role B with the same room name.");
        fflush(stdout);
        if (receive_text(incoming, message) < 0 || strcmp(message, "/ready") != 0)
            goto cleanup;
    } else if (send_text(outgoing, "/ready") < 0) {
        goto cleanup;
    }

    int send_next = owner;
    while (!interrupted) {
        if (send_next) {
            if (chat_input(argv[1], message) < 0 || send_text(outgoing, message) < 0)
                break;
        } else {
            if (receive_text(incoming, message) < 0)
                break;
            printf("received from %s: %s\n", owner ? "B" : "A", message);
            fflush(stdout);
        }
        if (strcmp(message, "/quit") == 0) {
            result = 0;
            break;
        }
        send_next = !send_next;
    }

cleanup:
    if (interrupted) {
        /* Best effort: notify a peer that already has the queue open. */
        struct mq_attr nonblocking = { .mq_flags = O_NONBLOCK };
        if (mq_setattr(outgoing, &nonblocking, NULL) == 0)
            send_text(outgoing, "/quit");
    } else if (result) {
        fputs("Message queue chat ended with an I/O or handshake error.\n", stderr);
    }
    mq_close(a_queue);
    mq_close(b_queue);
    if (owner) {
        mq_unlink(a_name);
        mq_unlink(b_name);
    }
    return interrupted ? 128 + interrupted : result;
}
