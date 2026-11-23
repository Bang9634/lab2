#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Levenshtein distance counts insertions, deletions and substitutions. */
static int distance(const char *target, const char *typed, size_t *errors)
{
    size_t n = strlen(target);
    size_t *cost = malloc((n + 1) * sizeof(*cost));
    if (!cost)
        return -1;
    for (size_t j = 0; j <= n; ++j)
        cost[j] = j;
    for (size_t i = 1; typed[i - 1] != '\0'; ++i) {
        size_t diagonal = cost[0];
        cost[0] = i;
        for (size_t j = 1; j <= n; ++j) {
            size_t old = cost[j];
            size_t best = diagonal + (target[j - 1] != typed[i - 1]);
            if (old + 1 < best)
                best = old + 1;
            if (cost[j - 1] + 1 < best)
                best = cost[j - 1] + 1;
            cost[j] = best;
            diagonal = old;
        }
    }
    *errors = cost[n];
    free(cost);
    return 0;
}

int main(void)
{
    const char *sentences[] = {
        "Linux files have permissions.",
        "A pipe connects two processes.",
        "Shared memory needs synchronization."
    };
    char *line = NULL;
    size_t capacity = 0;
    size_t typed_total = 0, error_total = 0;
    double seconds = 0.0;

    puts("Type each English sentence and press Enter.");
    for (size_t i = 0; i < sizeof(sentences) / sizeof(sentences[0]); ++i) {
        printf("\n%zu. %s\n> ", i + 1, sentences[i]);
        fflush(stdout);
        struct timespec start, end;
        if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) {
            perror("clock_gettime");
            free(line);
            return 1;
        }
        if (getline(&line, &capacity, stdin) < 0) {
            fputs("Input ended before all sentences were typed.\n", stderr);
            free(line);
            return 1;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &end) < 0) {
            perror("clock_gettime");
            free(line);
            return 1;
        }
        line[strcspn(line, "\r\n")] = '\0';
        for (const unsigned char *character = (const unsigned char *)line;
             *character; ++character) {
            if (*character > 127) {
                fputs("Use ASCII characters for the English typing exercise.\n", stderr);
                free(line);
                return 1;
            }
        }
        seconds += (double)(end.tv_sec - start.tv_sec)
                   + (end.tv_nsec - start.tv_nsec) / 1e9;

        size_t errors;
        if (distance(sentences[i], line, &errors) < 0) {
            perror("malloc");
            free(line);
            return 1;
        }
        typed_total += strlen(line);
        error_total += errors;
        printf("Sentence errors: %zu\n", errors);
    }
    printf("\nErrors: %zu | Typed chars: %zu | Time: %.2f s | Average CPM: %.1f\n",
           error_total, typed_total, seconds,
           seconds > 0.0 ? typed_total * 60.0 / seconds : 0.0);
    free(line);
    return 0;
}
