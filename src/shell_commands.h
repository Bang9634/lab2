#ifndef LAB2_SHELL_COMMANDS_H
#define LAB2_SHELL_COMMANDS_H

/* Returns -1 when the shell should try execvp. */
int run_command(char *const *args);
int change_directory(char *const *args);

#endif
