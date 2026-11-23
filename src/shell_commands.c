#include "shell_commands.h"
#include "list_dir.h"
#include "io.h"
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int usage(const char *syntax)
{
    fprintf(stderr, "Usage: %s\n", syntax);
    return 2;
}

static int report(const char *name, int result)
{
    if (result < 0)
        perror(name);
    return result < 0;
}

/* Directory commands */
int change_directory(char *const *args)
{
    if (args[1] && args[2])
        return usage("cd [DIRECTORY]");
    const char *path = args[1] ? args[1] : getenv("HOME");
    if (!path) {
        fputs("cd: HOME is unset\n", stderr);
        return 1;
    }
    return report(path, chdir(path));
}

static int print_directory(char *const *args)
{
    if (args[1])
        return usage("pwd");
    size_t capacity = 256;
    for (;;) {
        char *path = malloc(capacity);
        if (!path)
            return report("malloc", -1);
        if (getcwd(path, capacity)) {
            puts(path);
            free(path);
            return 0;
        }
        int saved = errno;
        free(path);
        if (saved != ERANGE || capacity > (size_t)-1 / 2) {
            errno = saved;
            return report("getcwd", -1);
        }
        capacity *= 2;
    }
}

static int show_directory(char *const *args)
{
    int recursive = args[1] && strcmp(args[1], "-R") == 0;
    size_t index = recursive ? 2 : 1;
    if (args[index] && args[index + 1])
        return usage("ls [-R] [DIRECTORY]");
    return list_dir(args[index] ? args[index] : ".", recursive);
}

/* File and stream commands */
static int transfer_bytes(int source, int destination)
{
    char buffer[4096];
    for (;;) {
        ssize_t size = read(source, buffer, sizeof(buffer));
        if (size < 0 && errno == EINTR)
            continue;
        if (size <= 0)
            return size < 0 ? -1 : 0;
        if (write_all(destination, buffer, (size_t)size) < 0)
            return -1;
    }
}

static int copy_file(const char *from, const char *to)
{
    int source = open(from, O_RDONLY | O_NONBLOCK);
    if (source < 0)
        return report(from, -1);
    struct stat original, target;
    int result = 1;
    if (fstat(source, &original) < 0) {
        perror(from);
    } else if (!S_ISREG(original.st_mode)) {
        fprintf(stderr, "%s: expected a regular file\n", from);
    } else {
        int destination = open(to, O_WRONLY | O_CREAT | O_NONBLOCK, original.st_mode & 0777);
        if (destination < 0) {
            perror(to);
        } else {
            if (fstat(destination, &target) < 0) {
                perror(to);
            } else if (!S_ISREG(target.st_mode) ||
                       (original.st_dev == target.st_dev && original.st_ino == target.st_ino)) {
                fprintf(stderr, "%s: expected a different regular file\n", to);
            } else {
                result = report("cp", ftruncate(destination, 0) < 0
                                    ? -1 : transfer_bytes(source, destination));
            }
            if (close(destination) < 0)
                result = report(to, -1);
        }
    }
    close(source);
    return result;
}

static int show_file(char *const *args)
{
    if (!args[1])
        return report("cat", transfer_bytes(STDIN_FILENO, STDOUT_FILENO));
    int result = 0;
    for (size_t i = 1; args[i]; ++i) {
        int fd = open(args[i], O_RDONLY);
        if (fd < 0) {
            result |= report(args[i], -1);
            continue;
        }
        result |= report(args[i], transfer_bytes(fd, STDOUT_FILENO));
        close(fd);
    }
    return result;
}

static int find_lines(char *const *args)
{
    if (!args[1] || (args[2] && args[3]))
        return usage("grep PATTERN [FILE]");
    regex_t regex;
    int error = regcomp(&regex, args[1], REG_NOSUB | REG_NEWLINE);
    if (error) {
        char reason[256];
        regerror(error, &regex, reason, sizeof(reason));
        fprintf(stderr, "grep: %s\n", reason);
        return 2;
    }

    FILE *source = args[2] ? fopen(args[2], "r") : stdin;
    int result = 1;
    if (!source) {
        perror(args[2]);
        result = 2;
    } else {
        char *line = NULL;
        size_t capacity = 0;
        ssize_t size;
        while ((size = getline(&line, &capacity, source)) >= 0) {
            int match = regexec(&regex, line, 0, NULL, 0);
            if (match == REG_NOMATCH)
                continue;
            if (match || fwrite(line, 1, (size_t)size, stdout) != (size_t)size) {
                result = 2;
                break;
            }
            result = 0;
        }
        if (ferror(source)) {
            perror("grep");
            result = 2;
        }
        free(line);
        if (source != stdin)
            fclose(source);
    }
    regfree(&regex);
    return result;
}

int run_command(char *const *args)
{
    const char *name = args[0];
    if (!strcmp(name, "pwd"))
        return print_directory(args);
    if (!strcmp(name, "ls"))
        return show_directory(args);
    if (!strcmp(name, "cd"))
        return change_directory(args);
    if (!strcmp(name, "cat"))
        return show_file(args);
    if (!strcmp(name, "grep"))
        return find_lines(args);

    if (!strcmp(name, "mkdir") || !strcmp(name, "rmdir") || !strcmp(name, "rm")) {
        if (!args[1] || args[2]) {
            fprintf(stderr, "Usage: %s PATH\n", name);
            return 2;
        }
        if (!strcmp(name, "mkdir"))
            return report(args[1], mkdir(args[1], 0777));
        if (!strcmp(name, "rmdir"))
            return report(args[1], rmdir(args[1]));
        return report(args[1], unlink(args[1]));
    }
    if (!strcmp(name, "ln") || !strcmp(name, "cp") || !strcmp(name, "mv")) {
        if (!args[1] || !args[2] || args[3]) {
            fprintf(stderr, "Usage: %s SOURCE DESTINATION\n", name);
            return 2;
        }
        if (!strcmp(name, "ln"))
            return report(args[2], link(args[1], args[2]));
        if (!strcmp(name, "mv"))
            return report(args[2], rename(args[1], args[2]));
        return copy_file(args[1], args[2]);
    }
    return -1;
}
