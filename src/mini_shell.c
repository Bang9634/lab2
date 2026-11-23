#include "io.h"
#include "shell_commands.h"
#include <ctype.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>

#define MAX_COMMANDS 8
#define MAX_ARGS 64
#define MAX_JOBS 32

struct command {
    char *args[MAX_ARGS + 1];
    size_t count;
    const char *input;
    const char *output;
};

struct pipeline {
    struct command commands[MAX_COMMANDS];
    size_t count;
    int background;
};

struct member {
    pid_t pid;
    int stopped;
};

struct job {
    pid_t group;
    pid_t last;
    struct member members[MAX_COMMANDS];
    size_t remaining;
    size_t stopped;
    int last_status;
    struct termios modes;
    int saved_modes;
};

struct shell {
    int interactive;
    pid_t group;
    struct termios modes;
    struct job jobs[MAX_JOBS];
};

enum token { END, WORD, PIPE, INPUT, OUTPUT, BACKGROUND, INVALID };

static enum token operator_token(char ch)
{
    switch (ch) {
    case '|': return PIPE;
    case '<': return INPUT;
    case '>': return OUTPUT;
    case '&': return BACKGROUND;
    default:  return WORD;
    }
}

/* Decode one word into separate storage, leaving the input available for scanning. */
static enum token next_token(const char **input, char **output, char **word)
{
    const char *read = *input;
    while (isspace((unsigned char)*read))
        ++read;
    if (!*read) {
        *input = read;
        return END;
    }
    enum token token = operator_token(*read);
    if (token != WORD) {
        *input = read + 1;
        return token;
    }

    char quote = '\0';
    *word = *output;
    for (; *read; ++read) {
        char ch = *read;
        if (!quote && (isspace((unsigned char)ch) || operator_token(ch) != WORD))
            break;
        if (ch == '\\' && quote != '\'' &&
            (quote != '"' || (read[1] && strchr("\\\"$`", read[1])))) {
            if (!read[1])
                return INVALID;
            *(*output)++ = *++read;
        } else if (ch == quote) {
            quote = '\0';
        } else if (!quote && (ch == '\'' || ch == '"')) {
            quote = ch;
        } else {
            *(*output)++ = ch;
        }
    }
    if (quote)
        return INVALID;
    *(*output)++ = '\0';
    *input = read;
    return WORD;
}

static int parse(const char *input, char *storage, struct pipeline *pipeline)
{
    pipeline->count = 1;
    struct command *current = &pipeline->commands[0];
    for (;;) {
        char *word;
        enum token token = next_token(&input, &storage, &word);
        switch (token) {
        case END:
            if (current->count)
                return 1;
            return pipeline->count == 1 && !current->input && !current->output ? 0 : -1;
        case WORD:
            if (current->count == MAX_ARGS)
                return -1;
            current->args[current->count++] = word;
            break;
        case PIPE:
            if (!current->count || pipeline->count == MAX_COMMANDS)
                return -1;
            current = &pipeline->commands[pipeline->count++];
            break;
        case INPUT:
        case OUTPUT: {
            const char **path = token == INPUT ? &current->input : &current->output;
            if (*path || next_token(&input, &storage, &word) != WORD)
                return -1;
            *path = word;
            break;
        }
        case BACKGROUND:
            pipeline->background = 1;
            return current->count && next_token(&input, &storage, &word) == END ? 1 : -1;
        case INVALID:
            return -1;
        }
    }
}

static const int terminal_signals[] = { SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU };

static void set_terminal_signals(void (*handler)(int))
{
    struct sigaction action = { .sa_handler = handler };
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof(terminal_signals) / sizeof(terminal_signals[0]); ++i)
        sigaction(terminal_signals[i], &action, NULL);
}

static int terminal_to(pid_t group)
{
    int result;
    do {
        result = tcsetpgrp(STDIN_FILENO, group);
    } while (result < 0 && errno == EINTR);
    if (result < 0)
        perror("tcsetpgrp");
    return result;
}

static int redirect_fd(const char *path, int target, int flags)
{
    int fd = open(path, flags, 0666);
    if (fd < 0 || (fd != target && dup2(fd, target) < 0)) {
        perror(path);
        if (fd >= 0)
            close(fd);
        return -1;
    }
    if (fd != target)
        close(fd);
    return 0;
}

static void child_command(const struct command *command, int input, int output,
                          int unused, int gate[2], pid_t group, int null_input)
{
    close(gate[1]);
    if (setpgid(0, group) < 0) {
        perror("setpgid");
        _exit(1);
    }
    set_terminal_signals(SIG_DFL);

    /* Wait until the parent has assigned every child and the foreground terminal. */
    char unused_byte;
    if (read_exact(gate[0], &unused_byte, 1) < 0)
        _exit(1);
    close(gate[0]);
    if (unused >= 0)
        close(unused);

    if ((input >= 0 && dup2(input, STDIN_FILENO) < 0) ||
        (output >= 0 && dup2(output, STDOUT_FILENO) < 0)) {
        perror("dup2");
        _exit(1);
    }
    if (input >= 0)
        close(input);
    if (output >= 0)
        close(output);
    if (command->input && redirect_fd(command->input, STDIN_FILENO, O_RDONLY) < 0)
        _exit(1);
    if (!command->input && input < 0 && null_input &&
        redirect_fd("/dev/null", STDIN_FILENO, O_RDONLY) < 0)
        _exit(1);
    if (command->output && redirect_fd(command->output, STDOUT_FILENO,
                                       O_WRONLY | O_CREAT | O_TRUNC) < 0)
        _exit(1);

    int status = run_command(command->args);
    if (status >= 0) {
        if (fflush(stdout) == EOF && status == 0)
            status = 1;
        _exit(status);
    }
    execvp(command->args[0], command->args);
    int saved = errno;
    perror(command->args[0]);
    _exit(saved == ENOENT ? 127 : 126);
}

static int launch(const struct pipeline *pipeline, struct job *job, int gate[2])
{
    int input = -1;
    for (size_t i = 0; i < pipeline->count; ++i) {
        int output[2] = {-1, -1};
        if (i + 1 < pipeline->count && pipe(output) < 0) {
            perror("pipe");
            if (input >= 0)
                close(input);
            return -1;
        }
        pid_t child = fork();
        if (child == 0)
            child_command(&pipeline->commands[i], input, output[1], output[0],
                          gate, job->group, pipeline->background);
        if (input >= 0)
            close(input);
        if (output[1] >= 0)
            close(output[1]);
        input = output[0];
        if (child < 0) {
            perror("fork");
            if (input >= 0)
                close(input);
            return -1;
        }
        if (!job->group)
            job->group = child;
        job->last = child;
        job->members[i].pid = child;
        ++job->remaining;
        if (setpgid(child, job->group) < 0) {
            perror("setpgid");
            if (input >= 0)
                close(input);
            return -1;
        }
    }
    return 0;
}

static void update_job(struct job *job, pid_t pid, int status)
{
    for (size_t i = 0; i < MAX_COMMANDS; ++i) {
        struct member *member = &job->members[i];
        if (member->pid != pid)
            continue;
        job->stopped -= (size_t)member->stopped;
        member->stopped = WIFSTOPPED(status);
        job->stopped += (size_t)member->stopped;
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            member->pid = 0;
            --job->remaining;
            if (pid == job->last)
                job->last_status = WIFEXITED(status) ? WEXITSTATUS(status)
                                                    : 128 + WTERMSIG(status);
        }
        return;
    }
}

static void reap_background(struct shell *shell)
{
    for (size_t i = 0; i < MAX_JOBS; ++i) {
        struct job *job = &shell->jobs[i];
        if (!job->group)
            continue;
        int status;
        pid_t child;
        while ((child = waitpid(-job->group, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
            update_job(job, child, status);
        if (!job->remaining) {
            if (shell->interactive)
                printf("[%zu] Done (%d) pgid=%ld\n", i + 1, job->last_status, (long)job->group);
            *job = (struct job){0};
        }
    }
}

static void wait_foreground(struct shell *shell, struct job *job, size_t slot)
{
    while (job->remaining > job->stopped) {
        int status;
        pid_t child = waitpid(-job->group, &status, WUNTRACED);
        if (child < 0 && errno == EINTR)
            continue;
        if (child < 0) {
            perror("waitpid");
            break;
        }
        update_job(job, child, status);
        if (WIFSTOPPED(status))
            kill(-job->group, SIGSTOP);
    }

    if (shell->interactive) {
        terminal_to(shell->group);
        if (job->stopped)
            job->saved_modes = tcgetattr(STDIN_FILENO, &job->modes) == 0;
        tcsetattr(STDIN_FILENO, TCSADRAIN, &shell->modes);
    }
    if (job->remaining)
        printf("[%zu] Stopped pgid=%ld\n", slot + 1, (long)job->group);
    else
        *job = (struct job){0};
}

static void start_job(struct shell *shell, const struct pipeline *pipeline)
{
    size_t slot = 0;
    while (slot < MAX_JOBS && shell->jobs[slot].group)
        ++slot;
    if (slot == MAX_JOBS) {
        fputs("Too many active jobs. Use jobs or fg.\n", stderr);
        return;
    }
    struct job *job = &shell->jobs[slot];
    int gate[2];
    if (pipe(gate) < 0) {
        perror("pipe");
        return;
    }
    int failed = launch(pipeline, job, gate) < 0;
    if (!failed && shell->interactive && !pipeline->background)
        failed = terminal_to(job->group) < 0;
    if (failed) {
        for (size_t i = 0; i < MAX_COMMANDS; ++i)
            if (job->members[i].pid)
                kill(job->members[i].pid, SIGKILL);
    }

    close(gate[0]);
    close(gate[1]);
    if (failed) {
        for (size_t i = 0; i < MAX_COMMANDS; ++i)
            if (job->members[i].pid)
                while (waitpid(job->members[i].pid, NULL, 0) < 0 && errno == EINTR) {
                }
        *job = (struct job){0};
    } else if (pipeline->background) {
        printf("[%zu] Background pgid=%ld\n", slot + 1, (long)job->group);
    } else {
        wait_foreground(shell, job, slot);
    }
}

static void show_jobs(const struct shell *shell)
{
    for (size_t i = 0; i < MAX_JOBS; ++i) {
        const struct job *job = &shell->jobs[i];
        if (job->group)
            printf("[%zu] %s pgid=%ld\n", i + 1,
                   job->stopped == job->remaining ? "Stopped" : "Running", (long)job->group);
    }
}

static void resume_foreground(struct shell *shell, const char *number)
{
    char *end;
    long index = number ? strtol(number, &end, 10) : 0;
    if (!number || *end || index < 1 || index > MAX_JOBS ||
        !shell->jobs[index - 1].group) {
        fputs("Usage: fg JOB_NUMBER (see jobs)\n", stderr);
        return;
    }
    struct job *job = &shell->jobs[index - 1];
    if (shell->interactive) {
        if (terminal_to(job->group) < 0)
            return;
        if (job->saved_modes)
            tcsetattr(STDIN_FILENO, TCSADRAIN, &job->modes);
    }
    if (kill(-job->group, SIGCONT) < 0 && errno != ESRCH)
        perror("SIGCONT");
    for (size_t i = 0; i < MAX_COMMANDS; ++i)
        job->members[i].stopped = 0;
    job->stopped = 0;
    wait_foreground(shell, job, (size_t)index - 1);
}

/* Return 1 only when the parent should exit. */
static int execute(struct shell *shell, struct pipeline *pipeline)
{
    struct command *command = &pipeline->commands[0];
    char **args = command->args;
    const char *name = args[0];
    int parent_only = !strcmp(name, "exit") || !strcmp(name, "jobs") || !strcmp(name, "fg");
    if (pipeline->count != 1 || pipeline->background) {
        if (parent_only) {
            fprintf(stderr, "%s requires a foreground command.\n", name);
            return 0;
        }
        start_job(shell, pipeline);
        return 0;
    }
    if (!parent_only && strcmp(name, "cd")) {
        start_job(shell, pipeline);
        return 0;
    }
    if (command->input || command->output) {
        fprintf(stderr, "%s does not accept redirection in this shell.\n", name);
        return 0;
    }
    if (!strcmp(name, "cd")) {
        change_directory(args);
    } else if (!strcmp(name, "fg")) {
        if (args[1] && args[2])
            fputs("Usage: fg JOB_NUMBER\n", stderr);
        else
            resume_foreground(shell, args[1]);
    } else if (args[1]) {
        fprintf(stderr, "Usage: %s\n", name);
    } else if (!strcmp(name, "jobs")) {
        show_jobs(shell);
    } else {
        return 1;
    }
    return 0;
}

int main(void)
{
    setlocale(LC_COLLATE, "");
    struct shell shell = { .interactive = isatty(STDIN_FILENO), .group = getpgrp() };
    if (shell.interactive) {
        if (tcgetpgrp(STDIN_FILENO) != shell.group ||
            tcgetattr(STDIN_FILENO, &shell.modes) < 0) {
            fputs("Run mini_shell in the foreground of a terminal.\n", stderr);
            return 1;
        }
        set_terminal_signals(SIG_IGN);
    }

    char *line = NULL;
    size_t capacity = 0;
    for (;;) {
        reap_background(&shell);
        if (shell.interactive) {
            fputs("lab2$ ", stdout);
            fflush(stdout);
        }
        ssize_t length = getline(&line, &capacity, stdin);
        if (length < 0)
            break;
        char *storage = malloc((size_t)length + 1);
        if (!storage) {
            perror("malloc");
            break;
        }
        struct pipeline pipeline = {0};
        int parsed = parse(line, storage, &pipeline);
        int exit_requested = parsed > 0 && execute(&shell, &pipeline);
        if (parsed < 0)
            fputs("Invalid or unsupported command syntax.\n", stderr);
        free(storage);
        if (exit_requested)
            break;
    }
    free(line);
    reap_background(&shell);
    for (size_t i = 0; i < MAX_JOBS; ++i) {
        if (shell.jobs[i].group) {
            kill(-shell.jobs[i].group, SIGHUP);
            kill(-shell.jobs[i].group, SIGCONT);
        }
    }
    return 0;
}
