#include "list_dir.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int visible(const struct dirent *entry)
{
    return entry->d_name[0] != '.';
}

static int sorted(const struct dirent **left, const struct dirent **right)
{
    const char *a = (*left)->d_name;
    const char *b = (*right)->d_name;
    int order = strcoll(a, b);
    return order ? order : strcmp(a, b);
}

int list_dir(const char *path, int recursive)
{
    struct dirent **entries;
    int count = scandir(path, &entries, visible, sorted);
    if (count < 0) {
        perror(path);
        return 1;
    }
    if (recursive)
        printf("%s:\n", path);
    for (int i = 0; i < count; ++i)
        puts(entries[i]->d_name);

    int result = 0;
    for (int i = 0; recursive && i < count; ++i) {
        const char *name = entries[i]->d_name;
        const char *separator = path[strlen(path) - 1] == '/' ? "" : "/";
        size_t size = strlen(path) + strlen(separator) + strlen(name) + 1;
        char *child = malloc(size);
        if (!child) {
            perror("malloc");
            result = 1;
            break;
        }
        snprintf(child, size, "%s%s%s", path, separator, name);
        struct stat info;
        if (lstat(child, &info) < 0) {
            perror(child);
            result = 1;
        } else if (S_ISDIR(info.st_mode)) {
            putchar('\n');
            result |= list_dir(child, 1);
        }
        free(child);
    }

    for (int i = 0; i < count; ++i)
        free(entries[i]);
    free(entries);
    return result;
}
