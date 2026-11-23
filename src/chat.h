#ifndef LAB2_CHAT_H
#define LAB2_CHAT_H

#include <stdio.h>
#include <string.h>

#define CHAT_SIZE 256

/* Read at most 254 characters and discard the rest of an overlong line. */
static inline int chat_input(const char *role, char message[CHAT_SIZE])
{
    for (;;) {
        printf("%s> ", role);
        fflush(stdout);
        if (!fgets(message, CHAT_SIZE, stdin)) {
            if (ferror(stdin))
                return -1;
            strcpy(message, "/quit");
            return 0;
        }
        size_t length = strlen(message);
        if (length > 0 && message[length - 1] == '\n') {
            message[length - 1] = '\0';
            return 0;
        }
        if (feof(stdin))
            return 0;
        int ch;
        do {
            ch = getchar();
        } while (ch != '\n' && ch != EOF);
        fputs("Message is too long. Limit: 254 bytes per line.\n", stderr);
    }
}

#endif
