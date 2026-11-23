#include "io.h"
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(void)
{
    char dir[] = "./file-demo-XXXXXX";
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 1;
    }

    char path[sizeof(dir) + sizeof("/note.txt")];
    snprintf(path, sizeof(path), "%s/note.txt", dir);
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0640);
    int result = 1;
    DIR *stream = NULL;
    struct stat info;
    const char sample[] = "Lab2 file I/O\n";
    char bytes[sizeof(sample)] = {0};

    if (fd < 0 || write_all(fd, sample, sizeof(sample) - 1) < 0 ||
        lseek(fd, 0, SEEK_SET) < 0 ||
        read_exact(fd, bytes, sizeof(sample) - 1) != 1 || fstat(fd, &info) < 0) {
        perror("file I/O");
        goto cleanup;
    }
    printf("read: %s", bytes);
    printf("fstat: size=%jd mode=%04o\n", (intmax_t)info.st_size,
           (unsigned int)(info.st_mode & 0777));

    stream = opendir(dir);
    if (!stream) {
        perror("opendir");
        goto cleanup;
    }
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(stream);
        if (!entry) {
            if (errno) {
                perror("readdir");
                goto cleanup;
            }
            break;
        }
        printf("readdir: %s\n", entry->d_name);
    }
    result = 0;

cleanup:
    if (stream && closedir(stream) < 0) {
        perror("closedir");
        result = 1;
    }
    if (fd >= 0 && close(fd) < 0) {
        perror("close");
        result = 1;
    }
    if (fd >= 0 && unlink(path) < 0) {
        perror("unlink");
        result = 1;
    }
    if (rmdir(dir) < 0) {
        perror("rmdir");
        result = 1;
    }
    if (result == 0)
        puts("Temporary file and directory removed.");
    return result;
}
